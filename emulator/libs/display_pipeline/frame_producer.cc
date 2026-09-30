// Copyright 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "goldfish/display/frame_producer.h"

#include <memory>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace goldfish::display {

CaptureCompletion& CaptureCompletion::operator=(CaptureCompletion&& other) noexcept {
    if (this != &other) {
        CaptureCompletion replaced = std::move(*this);  // Cancelled when it goes out of scope.
        callback_ = std::move(other.callback_);
        other.callback_ = nullptr;
    }
    return *this;
}

CaptureCompletion::~CaptureCompletion() {
    if (callback_ != nullptr) std::move(*this).operator()(absl::CancelledError("Capture dropped"));
}

void CaptureCompletion::operator()(absl::StatusOr<SlotLease> frame) && {
    Callback callback = std::move(callback_);  // Invoked at most once.
    callback_ = nullptr;
    if (callback != nullptr) std::move(callback)(std::move(frame));
}

FrameProducer::FrameProducer(async::EventLoop* executor, std::weak_ptr<IFrameSink> sink)
        : executor_(executor), sink_(std::move(sink)) {}

FrameProducer::~FrameProducer() {
    Shutdown();
}

void FrameProducer::SetSource(std::shared_ptr<IFrameSource> source) {
    absl::MutexLock state_lock(&state_mutex_);
    if (shut_down_) return;
    source_ = std::move(source);
    source_warm_ = false;  // A new source has been told nothing yet.
}

void FrameProducer::Invalidate() {
    {
        absl::MutexLock state_lock(&state_mutex_);
        dirty_ = true;
        // If a pump (including Refresh re-entering Invalidate) or a capture is
        // already active, its completion will pick up the dirty flag.
        if (pumping_ || in_flight_ != 0) return;
    }
    Pump(/*refresh=*/false);
}

void FrameProducer::Reset() {
    absl::MutexLock publish_lock(&publish_mutex_);
    {
        absl::ReaderMutexLock state_lock(&state_mutex_);
        if (shut_down_) return;  // Shutdown was the last reset.
    }
    ResetLocked();
}

void FrameProducer::ResetLocked() {
    {
        absl::MutexLock state_lock(&state_mutex_);
        ++generation_;   // Frames from earlier captures are dropped from now on.
        in_flight_ = 0;  // Its completion no longer matches.
        dirty_ = requested_ = false;
    }
    // Scoped so that a sink whose last reference is this one dies guarded.
    if (const std::shared_ptr<IFrameSink> sink = sink_.lock()) sink->OnProducerReset();
}

void FrameProducer::NoContent() {
    absl::MutexLock publish_lock(&publish_mutex_);
    {
        absl::ReaderMutexLock state_lock(&state_mutex_);
        if (shut_down_) return;
    }
    if (const std::shared_ptr<IFrameSink> sink = sink_.lock()) sink->PublishNoContent();
}

bool FrameProducer::warm() const {
    absl::MutexLock state_lock(&state_mutex_);
    return warm_;
}

void FrameProducer::Shutdown() {
    std::shared_ptr<IFrameSource> source;  // Destroyed after the locks are released.
    bool first;
    {
        // Not under publish_mutex_: a source call may complete a capture inline.
        absl::MutexLock state_lock(&state_mutex_);
        first = !std::exchange(shut_down_, true);
        state_mutex_.Await(absl::Condition(this, &FrameProducer::NoSourceCalls));
        source = std::move(source_);
    }
    if (!first) return;
    absl::MutexLock publish_lock(&publish_mutex_);
    ResetLocked();
}

void FrameProducer::SetDemand(bool warm) {
    {
        absl::MutexLock state_lock(&state_mutex_);
        if (shut_down_) return;
        warm_ = warm;
        if (!warm) dirty_ = requested_ = false;  // A cold display owes no capture.
    }
    if (executor_ == nullptr) return;
    const absl::Status status = executor_->Post([weak = weak_from_this(), warm] {
        if (std::shared_ptr<FrameProducer> self = weak.lock()) self->ApplyDemand(warm);
    });
    if (!status.ok()) LOG_FIRST_N(ERROR, 1) << "Frame producer can't apply demand: " << status;
}

void FrameProducer::RequestFrame() {
    {
        absl::MutexLock state_lock(&state_mutex_);
        if (shut_down_ || !warm_) return;
        requested_ = true;
        if (!TakePostLocked()) return;
    }
    Post(&FrameProducer::RunPostedPump);
}

void FrameProducer::ApplyDemand(bool warm) {
    std::shared_ptr<IFrameSource> source;
    {
        absl::MutexLock state_lock(&state_mutex_);
        // Serialize source calls: Invalidate may be pumping on another thread.
        state_mutex_.Await(absl::Condition(this, &FrameProducer::NotPumping));
        // A later SetDemand changed it again; its own task applies it.
        if (shut_down_ || source_ == nullptr || warm_ != warm) return;
        if (warm) requested_ = true;  // Demand starting implies one frame.
        // A flip and flip back the source never saw isn't a change to it.
        if (std::exchange(source_warm_, warm) != warm) {
            pumping_ = true;
            ++source_calls_;
            source = source_;
        }
    }
    bool post = false;
    if (source != nullptr) {
        source->OnDemandChanged(warm);
        absl::MutexLock state_lock(&state_mutex_);
        if (warm && !shut_down_ && warm_ == warm) requested_ = true;
        pumping_ = false;
        --source_calls_;
        // Content invalidated meanwhile is picked up as a pump's end does.
        post = !warm && TakePostLocked();
    }
    if (warm) Pump(/*refresh=*/true);
    if (post) Post(&FrameProducer::RunPostedPump);
}

void FrameProducer::RunPostedPump() {
    {
        absl::MutexLock state_lock(&state_mutex_);
        pending_post_ = false;
    }
    Pump(/*refresh=*/true);
}

void FrameProducer::Pump(bool refresh) {
    std::shared_ptr<IFrameSource> source;
    {
        absl::MutexLock state_lock(&state_mutex_);
        if (!PumpDueLocked()) return;
        // Fresh guest content (Invalidate) satisfies a request without a
        // refresh.
        refresh = std::exchange(requested_, false) && refresh;
        pumping_ = true;
        ++source_calls_;
        source = source_;
    }
    // No lock is held across Refresh or Capture, so both may re-enter the
    // producer and a capture may complete inline.
    if (refresh) source->Refresh();
    uint64_t capture;
    uint64_t generation;
    {
        absl::MutexLock state_lock(&state_mutex_);
        dirty_ = false;  // Content invalidated by the refresh is captured now.
        capture = in_flight_ = next_capture_++;
        generation = generation_;
        ++outstanding_;  // Until the completion runs; destroying it runs it.
    }
    const IFrameSource::Start start = source->Capture(
            CaptureCompletion([weak = weak_from_this(), capture, generation,
                               capture_time = absl::Now()](absl::StatusOr<SlotLease> frame) {
                if (std::shared_ptr<FrameProducer> self = weak.lock()) {
                    self->OnCaptureDone(capture, generation, std::move(frame), capture_time);
                }
            }));
    // Published before the pump ends: a later pump's frame is then never
    // overwritten by this blank.
    const bool stale_blank =
            start == IFrameSource::Start::kNoContent && !PublishNoContent(generation);
    bool post;
    {
        absl::MutexLock state_lock(&state_mutex_);
        // Content a source can't capture now is its to re-request.
        if (start != IFrameSource::Start::kStarted && in_flight_ == capture) in_flight_ = 0;
        // As for a stale frame, the source may have seen post-reset content.
        if (stale_blank && warm_ && !shut_down_) requested_ = true;
        pumping_ = false;
        --source_calls_;
        post = TakePostLocked();
    }
    if (post) Post(&FrameProducer::RunPostedPump);
}

bool FrameProducer::PublishNoContent(uint64_t generation) {
    absl::MutexLock publish_lock(&publish_mutex_);
    {
        absl::ReaderMutexLock state_lock(&state_mutex_);
        if (shut_down_ || generation != generation_) return false;
    }
    if (const std::shared_ptr<IFrameSink> sink = sink_.lock()) sink->PublishNoContent();
    return true;
}

void FrameProducer::OnCaptureDone(uint64_t capture, uint64_t generation,
                                  absl::StatusOr<SlotLease> frame, absl::Time capture_time) {
    bool stale = false;
    if (frame.ok()) {  // Publish only completed captures.
        absl::MutexLock publish_lock(&publish_mutex_);
        {
            absl::ReaderMutexLock state_lock(&state_mutex_);
            stale = generation != generation_;
        }
        const std::shared_ptr<IFrameSink> sink = stale ? nullptr : sink_.lock();
        if (sink != nullptr) sink->PublishFrame(*std::move(frame), capture_time);
    }
    // Ended only now, so a later capture never publishes before this one.
    {
        absl::MutexLock state_lock(&state_mutex_);
        --outstanding_;  // Decremented whether stale or not so Quiesce unblocks.
        // A reset may have come between the pump reading the generation and
        // the capture, which then holds post-reset content: capture it again.
        if (stale && warm_ && !shut_down_) requested_ = true;
        if (capture == in_flight_) {
            in_flight_ = 0;
        } else if (!stale) {
            return;  // Already ended.
        }
        if (!TakePostLocked()) return;  // A pump on the stack posts itself.
    }
    Post(&FrameProducer::RunPostedPump);
}

bool FrameProducer::Quiesce(absl::Duration timeout) {
    absl::MutexLock state_lock(&state_mutex_);
    return state_mutex_.AwaitWithTimeout(absl::Condition(this, &FrameProducer::Quiescent), timeout);
}

bool FrameProducer::PumpDueLocked() const {
    return !shut_down_ && source_ != nullptr && !pumping_ && in_flight_ == 0 &&
           (dirty_ || requested_);
}

bool FrameProducer::TakePostLocked() {
    if (pending_post_ || executor_ == nullptr || !PumpDueLocked()) return false;
    pending_post_ = true;
    return true;
}

void FrameProducer::Post(void (FrameProducer::*task)()) {
    const absl::Status status = executor_->Post([weak = weak_from_this(), task] {
        if (std::shared_ptr<FrameProducer> self = weak.lock()) ((*self).*task)();
    });
    if (status.ok()) return;
    LOG_FIRST_N(ERROR, 1) << "Frame producer can't post a pump: " << status;
    // No pump is queued after all, so clear pending_post_ to let future requests post.
    absl::MutexLock state_lock(&state_mutex_);
    pending_post_ = false;
}

}  // namespace goldfish::display
