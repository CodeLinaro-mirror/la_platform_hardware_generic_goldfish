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

#include "goldfish/display/latest_frame_pump.h"

#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"

namespace goldfish::display {

LatestFramePump::LatestFramePump(ResolveSourceFn resolve, async::EventLoop* worker,
                                 ProcessFrameFn process, FinishFn finish)
        : resolve_(std::move(resolve))
        , worker_(worker)
        , process_(std::move(process))
        , finish_(std::move(finish)) {
    CHECK_NE(worker_, nullptr);
}

void LatestFramePump::OnEvent(const FrameEvent& /*event*/) {
    bool should_schedule = false;
    {
        absl::MutexLock state_lock(&state_mutex_);
        if (finished_) {
            return;
        }
        dirty_ = true;
        if (!scheduled_) {
            scheduled_ = true;
            should_schedule = true;
        }
    }
    if (should_schedule) {
        absl::Status status = worker_->Post([weak = weak_from_this()] {
            if (std::shared_ptr<LatestFramePump> self = weak.lock()) {
                self->Drain();
            }
        });
        if (!status.ok()) {
            LOG(WARNING) << "Failed to schedule LatestFramePump::Drain: " << status;
        }
    }
}

void LatestFramePump::Drain() {
    for (;;) {
        {
            absl::MutexLock state_lock(&state_mutex_);
            if (!dirty_) {
                scheduled_ = false;
                return;
            }
            dirty_ = false;
        }
        ResolvedSource resolved = resolve_();
        if (!resolved.identity) {
            continue;
        }
        LatestFrame& latest = resolved.latest;
        if (latest.removed) {
            {
                absl::MutexLock state_lock(&state_mutex_);
                // An event that arrived during resolve_() may come from a
                // replacement display: resolve again before giving up.
                if (dirty_) {
                    continue;
                }
                finished_ = true;
                scheduled_ = false;
            }
            if (finish_) {
                finish_();
            }
            return;
        }
        if (!latest.frame) {
            continue;
        }
        const FrameEvent& ev = latest.frame->event();
        if (resolved.identity == last_display_ &&
            (ev.frame_id < last_id_ ||
             (ev.frame_id == last_id_ && ev.rotation == last_rotation_))) {
            continue;
        }
        last_display_ = std::move(resolved.identity);
        last_id_ = ev.frame_id;
        last_rotation_ = ev.rotation;
        process_(std::move(latest.frame));
    }
}

}  // namespace goldfish::display
