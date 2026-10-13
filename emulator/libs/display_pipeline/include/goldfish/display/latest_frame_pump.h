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

#include <memory>
#include <type_traits>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/display/display_frames.h"
#include "goldfish/display/frame.h"

namespace goldfish::display {

/**
 * Coalescing pull pump that bridges non-blocking display-loop FrameEvents to a
 * consumer's worker EventLoop.
 *
 * Data Flow:
 * Display listeners invoke OnEvent() on the display event loop, marking the
 * pump dirty and scheduling a single Drain() pass on `worker`. On `worker`, the
 * pump resolves the active display source, calls AcquireLatestFrame(), skips
 * frames whose FrameId and ImageRotation have already been processed on the
 * same display instance, and invokes `process` with the newest `FramePtr`. If
 * the resolved display has been removed and no OnEvent() arrived while
 * resolving, `finish` is invoked and the pump stops for good: `finish` runs at
 * most once, and later OnEvent() calls are ignored. An OnEvent() that arrives
 * while resolving a removed display triggers another resolve, so an event from
 * a replacement display is not lost.
 *
 * Invariants:
 * - At most one Drain() task is scheduled or running on `worker` at any time
 *   (`scheduled_` under `state_mutex_`).
 * - Any OnEvent() call that arrives while `process` is running sets
 *   `dirty_ = true`, guaranteeing another iteration on `worker` so the final
 *   frame (or rotation update) is never missed.
 *
 * Thread Safety:
 * OnEvent() is thread-safe and non-blocking. `resolve`, `process`, and `finish`
 * are invoked exclusively on `worker`.
 *
 * Ownership & Lifetimes:
 * Must be managed via `std::shared_ptr<LatestFramePump>`. Scheduled tasks hold
 * a `std::weak_ptr<LatestFramePump>`, so destroying listeners and then dropping
 * the `shared_ptr` safely cancels any pending worker task. A Drain() that is
 * already running holds a strong reference and is not waited for: dropping the
 * last external reference while `resolve`, `process`, or `finish` runs defers
 * destruction until Drain() returns, on `worker`. Before destroying state that
 * those callbacks capture, the owner must stop or join `worker`, or drop the
 * listener and wait for a running Drain() to return. `last_display_`
 * retains a `std::shared_ptr<const void>` to the resolved display on the worker
 * thread so display identity comparisons (`display == last_display_`) are
 * immune to ABA pointer reuse when displays are destroyed and recreated.
 */
class LatestFramePump : public std::enable_shared_from_this<LatestFramePump> {
  public:
    struct ResolvedSource {
        std::shared_ptr<const void> identity;
        LatestFrame latest;
    };
    using ResolveSourceFn = absl::AnyInvocable<ResolvedSource()>;
    using ProcessFrameFn = absl::AnyInvocable<void(FramePtr)>;
    using FinishFn = absl::AnyInvocable<void()>;

    /**
     * @param resolve Returns a `std::shared_ptr<T>` to the active display (e.g.
     *        `IDisplay` or `DisplayFrames`) exposing `AcquireLatestFrame()`.
     * @param worker The consumer's worker EventLoop where reads and encoding run.
     * @param process Invoked on `worker` with each new (or rotated) FramePtr.
     * @param finish Optional callback invoked on `worker`, at most once, when the
     *        display is removed.
     */
    template <
            typename ResolveFn,
            typename = std::enable_if_t<!std::is_same_v<std::decay_t<ResolveFn>, ResolveSourceFn>>>
    LatestFramePump(ResolveFn resolve, async::EventLoop* worker, ProcessFrameFn process,
                    FinishFn finish = {})
            : LatestFramePump(
                      ResolveSourceFn([resolve = std::move(resolve)]() mutable -> ResolvedSource {
                          auto display = resolve();
                          if (!display) {
                              return {};
                          }
                          LatestFrame latest = display->AcquireLatestFrame();
                          return ResolvedSource{.identity = std::move(display),
                                                .latest = std::move(latest)};
                      }),
                      worker, std::move(process), std::move(finish)) {}

    LatestFramePump(ResolveSourceFn resolve, async::EventLoop* worker, ProcessFrameFn process,
                    FinishFn finish = {});

    /**
     * Marks the pump dirty for a new frame or rotation event without blocking
     * the caller.
     *
     * @param event The frame notification from AddFrameListener.
     */
    void OnEvent(const FrameEvent& event) ABSL_LOCKS_EXCLUDED(state_mutex_);

  private:
    void Drain() ABSL_LOCKS_EXCLUDED(state_mutex_);

    ResolveSourceFn resolve_;
    async::EventLoop* const worker_;
    ProcessFrameFn process_;
    FinishFn finish_;

    absl::Mutex state_mutex_;
    bool dirty_ ABSL_GUARDED_BY(state_mutex_) = false;
    bool scheduled_ ABSL_GUARDED_BY(state_mutex_) = false;
    bool finished_ ABSL_GUARDED_BY(state_mutex_) = false;

    std::shared_ptr<const void> last_display_;
    FrameId last_id_ = 0;
    ImageRotation last_rotation_ = ImageRotation::kRotation0;
};

}  // namespace goldfish::display
