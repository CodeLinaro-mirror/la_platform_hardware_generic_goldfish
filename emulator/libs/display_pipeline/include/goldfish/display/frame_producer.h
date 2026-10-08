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
#pragma once

#include <cstdint>
#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/display/frame.h"
#include "goldfish/display/frame_source.h"

namespace goldfish::display {

/**
 * Coalescing, latest-wins frame pump that coordinates an `IFrameSource` and
 * publishes captured frames to a display's `IFrameSink`.
 *
 * Architectural Role:
 * `FrameProducer` is the producer-side coordinator for a single display. It
 * serializes calls to the underlying `IFrameSource`, enforces at-most-one
 * capture in flight per generation, coalesces concurrent frame requests, and
 * fences stale frames across producer resets.
 *
 * Concept & Data Flow:
 * - **Push (`Invalidate()`):** Called when the guest flushes new display
 *   content. If idle, starts a capture immediately on the calling thread
 *   without calling `IFrameSource::Refresh()`. If a pump or capture is already
 *   in progress, sets `dirty_ = true` so another capture is scheduled as soon
 *   as the current one completes.
 * - **Pull (`RequestFrame()` / `SetDemand(true)`):** Called when a consumer
 *   requests a frame, a starved `SlotPool` regains a free slot, or demand
 *   starts. Posts at most one coalesced pump task onto `executor_`, which calls
 *   `IFrameSource::Refresh()` before `IFrameSource::Capture()`.
 * - **Reset & Publish Ordering:** Every `IFrameSink` call (`PublishFrame`,
 *   `OnProducerReset`, `PublishNoContent`) is serialized under
 *   `publish_mutex_`. `Reset()` increments `generation_` and calls
 *   `OnProducerReset()` while holding `publish_mutex_`, and `OnCaptureDone()`
 *   verifies `generation == generation_` under `publish_mutex_` before calling
 *   `PublishFrame()`. This guarantees that no frame from a capture initiated
 *   before a reset can ever be published after `OnProducerReset()`.
 *
 * Invariants (outside `state_mutex_`):
 * - `in_flight_ != 0` implies a capture started in the current `generation_`
 *   has not yet completed.
 * - `requested_ == true` implies `warm_ == true`.
 *
 * Thread Safety:
 * Fully thread-safe. At most one thread executes `IFrameSource` methods
 * (`OnDemandChanged`, `Refresh`, `Capture`) at any time (`pumping_` under
 * `state_mutex_`). Neither `state_mutex_` nor `publish_mutex_` is held while
 * invoking `IFrameSource` methods or posting to `executor_`, allowing
 * `Refresh()` to re-enter `Invalidate()` and synchronous CPU captures to
 * complete inline without deadlocking. Sink callbacks must not synchronously
 * call back into `FrameProducer` except via `SlotLease` destruction (whose
 * starvation recovery only calls `RequestFrame()`).
 *
 * Ownership & Lifetimes:
 * Owned by its display via `std::shared_ptr<FrameProducer>`; tasks posted to
 * `executor_` hold `std::weak_ptr<FrameProducer>`. The owning display must call
 * `Shutdown()` in its destructor before tearing down state referenced by the
 * `IFrameSource` hooks.
 */
class FrameProducer final : public IFrameProducer,
                            public std::enable_shared_from_this<FrameProducer> {
  public:
    /**
     * @param executor Event loop (typically the QEMU main loop) that executes
     *        posted pump tasks and demand changes; must outlive this object.
     *        May be `nullptr` in unit tests that drive `Invalidate()` inline.
     * @param sink Consumer frame hub (`DisplayFrames`) that receives published
     *        frames; held weakly and may expire before the producer.
     */
    FrameProducer(async::EventLoop* executor, std::weak_ptr<IFrameSink> sink);
    /** Calls `Shutdown()`. */
    ~FrameProducer() override;

    FrameProducer(const FrameProducer&) = delete;
    FrameProducer& operator=(const FrameProducer&) = delete;

    /**
     * Installs or replaces the `IFrameSource` during display initialization,
     * before any demand, request, or invalidation occurs. Does not trigger a
     * reset.
     */
    void SetSource(std::shared_ptr<IFrameSource> source) ABSL_LOCKS_EXCLUDED(state_mutex_);

    /**
     * Signals that new guest content is available.
     *
     * If no pump or capture is currently active, captures immediately on the
     * calling thread. If a pump or capture is already running (including
     * re-entrant calls from `IFrameSource::Refresh()`), marks the producer
     * dirty so a follow-up capture is scheduled when the active one finishes.
     */
    void Invalidate() ABSL_LOCKS_EXCLUDED(state_mutex_);

    /**
     * Resets the producer's capture state and notifies the sink via
     * `IFrameSink::OnProducerReset()`.
     *
     * Clears pending dirty/request flags, increments `generation_` so any
     * in-flight capture from before the reset is discarded upon completion, and
     * allows a new post-reset capture to start immediately without waiting for
     * the stale capture to finish.
     */
    void Reset() ABSL_LOCKS_EXCLUDED(publish_mutex_, state_mutex_);

    /**
     * Notifies the sink via `IFrameSink::PublishNoContent()` that the display
     * currently has no guest content to show.
     *
     * Not ordered against pumps: call it on the executor, where no pump can be
     * running, e.g. right after `Reset()` on a surface switch.
     */
    void NoContent() ABSL_LOCKS_EXCLUDED(publish_mutex_, state_mutex_);

    /**
     * Blocks until all started captures have invoked or dropped their
     * `CaptureCompletion`, including captures that an earlier `Reset()` marked
     * stale, and no `IFrameSource` call is in progress (such as a pump inside
     * `Refresh()` that has yet to start its capture).
     *
     * Used before guest device reset, snapshot save, or snapshot load to ensure
     * no asynchronous capture is still accessing source resources. It does not
     * stop new pumps: the caller must keep new triggers out for the guarantee
     * to hold after it returns. Must never be called from inside an
     * `IFrameSource` or `IFrameSink` callback, nor while holding a lock a
     * source call may take (it would wait out `timeout`).
     *
     * @param timeout Maximum duration to wait for outstanding captures.
     * @return `true` if all captures finished before `timeout`, `false` on timeout.
     */
    bool Quiesce(absl::Duration timeout) ABSL_LOCKS_EXCLUDED(state_mutex_);

    /**
     * @return `true` while the display has active consumer demand. Used as a
     *         fast-path check in guest flush hooks to skip work when cold.
     */
    bool warm() const ABSL_LOCKS_EXCLUDED(state_mutex_);

    /**
     * Permanently stops the producer, waits for any in-progress `IFrameSource`
     * call on another thread to finish, releases the source, and issues a final
     * `OnProducerReset()` to the sink (since the sink may outlive the
     * producer).
     *
     * Idempotent. Must never be called from inside an `IFrameSource` or
     * `IFrameSink` callback.
     */
    void Shutdown() ABSL_LOCKS_EXCLUDED(publish_mutex_, state_mutex_);

    /**
     * Updates consumer demand state and posts `ApplyDemand()` to `executor_`.
     * Passing `warm = false` clears pending dirty and request flags. The
     * source is told only when the demand differs from what it last saw, but
     * every warm-up that is still current when applied captures a frame.
     */
    void SetDemand(bool warm) override ABSL_LOCKS_EXCLUDED(state_mutex_);

    /**
     * Coalesces and posts a pump task to `executor_` to refresh and capture the
     * source. Ignored while the producer is cold (`!warm()`).
     */
    void RequestFrame() override ABSL_LOCKS_EXCLUDED(state_mutex_);

  private:
    // Runs a pump if one is due and none is running.
    // @param refresh Whether an explicit request refreshes the source first.
    void Pump(bool refresh) ABSL_LOCKS_EXCLUDED(state_mutex_);
    void RunPostedPump() ABSL_LOCKS_EXCLUDED(state_mutex_);
    void ApplyDemand(bool warm) ABSL_LOCKS_EXCLUDED(state_mutex_);
    // Publishes `frame` unless a Reset came after `generation`, then ends
    // `capture`.
    void OnCaptureDone(uint64_t capture, uint64_t generation, absl::StatusOr<SlotLease> frame,
                       absl::Time capture_time) ABSL_LOCKS_EXCLUDED(publish_mutex_, state_mutex_);
    // Publishes a pump's no-content result unless a Reset came after
    // `generation` or the producer shut down. Returns whether it published.
    bool PublishNoContent(uint64_t generation) ABSL_LOCKS_EXCLUDED(publish_mutex_, state_mutex_);
    // Bumps the generation and calls OnProducerReset.
    void ResetLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(publish_mutex_)
            ABSL_LOCKS_EXCLUDED(state_mutex_);
    // Something is left to do and nothing runs.
    bool PumpDueLocked() const ABSL_SHARED_LOCKS_REQUIRED(state_mutex_);
    // Claims the pending post if a pump is due; the caller then posts.
    bool TakePostLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(state_mutex_);
    void Post(void (FrameProducer::*task)()) ABSL_LOCKS_EXCLUDED(state_mutex_);
    bool NoSourceCalls() const ABSL_SHARED_LOCKS_REQUIRED(state_mutex_) {
        return source_calls_ == 0;
    }
    bool Quiescent() const ABSL_SHARED_LOCKS_REQUIRED(state_mutex_) {
        return outstanding_ == 0 && source_calls_ == 0;
    }
    bool NotPumping() const ABSL_SHARED_LOCKS_REQUIRED(state_mutex_) { return !pumping_; }

    async::EventLoop* const executor_;
    const std::weak_ptr<IFrameSink> sink_;

    // Held across every sink call and taken before state_mutex_. Never taken
    // on the slot release path.
    absl::Mutex publish_mutex_ ABSL_ACQUIRED_BEFORE(state_mutex_);
    mutable absl::Mutex state_mutex_;
    std::shared_ptr<IFrameSource> source_ ABSL_GUARDED_BY(state_mutex_);
    bool shut_down_ ABSL_GUARDED_BY(state_mutex_) = false;
    bool warm_ ABSL_GUARDED_BY(state_mutex_) = false;
    bool source_warm_ ABSL_GUARDED_BY(state_mutex_) = false;  // Last told OnDemandChanged.
    bool dirty_ ABSL_GUARDED_BY(state_mutex_) = false;        // Invalidate since the last capture.
    bool requested_ ABSL_GUARDED_BY(state_mutex_) = false;    // RequestFrame since then.
    // A pump, or a demand change, is calling the source on some thread.
    bool pumping_ ABSL_GUARDED_BY(state_mutex_) = false;
    bool pending_post_ ABSL_GUARDED_BY(state_mutex_) = false;  // A posted pump hasn't run yet.
    uint64_t in_flight_ ABSL_GUARDED_BY(state_mutex_) = 0;     // The capture in flight; 0: none.
    uint64_t next_capture_ ABSL_GUARDED_BY(state_mutex_) = 1;
    // Bumped by every reset, under publish_mutex_ too; frames of captures started
    // before it are dropped.
    uint64_t generation_ ABSL_GUARDED_BY(state_mutex_) = 1;
    int source_calls_ ABSL_GUARDED_BY(state_mutex_) = 0;  // Source calls in progress.
    // Captures whose completion has neither run nor been dropped. Unlike
    // in_flight_, a Reset never clears it so Quiesce waits for stale captures.
    int outstanding_ ABSL_GUARDED_BY(state_mutex_) = 0;
};

}  // namespace goldfish::display
