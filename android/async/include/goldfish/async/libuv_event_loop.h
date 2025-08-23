// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may not use this file except in compliance with the License.
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

#include <functional>
#include <memory>
#include <queue>
#include <thread>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/async/event_loop.h"
#include "uv.h"

namespace goldfish::async {

/**
 * @brief A concrete implementation of the EventLoop interface using libuv.
 * @class LibuvEventLoop
 *
 * This class provides a thread-safe event loop that manages asynchronous tasks,
 * delayed posts, and repeating timers. It encapsulates the libuv C library,
 * exposing its functionality through the abstract EventLoop interface.
 *
 * @note Core Design Invariants
 * The correctness of this class relies on several key invariants that are
 * strictly enforced by its implementation. Understanding these is crucial for
- * reasoning about its behavior and for future modifications.
 *
 * @invariant **The Thread-Safety Invariant**
 * All direct interactions with the libuv C library (any function starting
 * with `uv_`) **must** be executed exclusively on the single thread that is
 * running `uv_run()`.
 * > **Justification:** This is the fundamental contract of the libuv library.
 * > This class enforces this by posting tasks from its public methods to be
 * > executed safely on the loop's own thread.
 *
 * @invariant **The Shutdown State Invariant**
 * The `mIsShuttingDown` atomic flag is monotonic: once `true`, it **will never**
 * revert to `false`. No new user tasks or timers will be accepted in this state.
 * > **Justification:** This ensures the loop has a well-defined terminal state,
 * > allowing it to wind down gracefully without accepting new work.
 *
 * ---
 * @note Internal `LibuvTimer` Invariants
 * The following invariants apply to the private `LibuvTimer` implementation,
 * whose correctness is essential to the overall system.
 * ---
 *
 * @invariant **The Lifecycle and Set-Membership Invariant**
 * For any living `LibuvTimer` object, its raw pointer (`this`) **must**
 * exist in the `mActiveTimers` set.
 * > **Justification:** The constructor adds the pointer and the destructor removes
 * > it. All modifications to the set are guarded by `mActiveTimersMutex`,
 * > ensuring that all active timers are tracked without exception.
 *
 * @invariant **The Handle Ownership Invariant**
 * For any active `uv_timer_t` handle, its `handle->data` field **must** point
 * to a heap-allocated `std::shared_ptr<LibuvTimer>` that co-owns the timer object.
 * > **Justification:** This is the core mechanism that keeps the C++ object
 * > alive during asynchronous libuv callbacks, preventing use-after-free errors.
 * > This ownership is only relinquished in the `deleteSharedPtrOnClose` callback.
 *
 * @invariant **The `uv_close` Uniqueness Invariant**
 * `uv_close()` is called on a timer's handle **if and only if** its internal
 * `mIsClosed` flag makes a single, atomic transition from `false` to `true`.
 * > **Justification:** The `if (!mIsClosed.exchange(true))` pattern guarantees
 * > that the expensive and final cleanup operation is triggered exactly once,
 * > preventing duplicate calls and resource management errors.
 */
class LibuvEventLoop : public EventLoop {
  public:
    LibuvEventLoop();
    ~LibuvEventLoop() override;

    // --- Prevent Copying ---
    LibuvEventLoop(const LibuvEventLoop&) = delete;
    LibuvEventLoop& operator=(const LibuvEventLoop&) = delete;

    // --- Moving is not supported for simplicity ---
    LibuvEventLoop(LibuvEventLoop&& other) noexcept = delete;
    LibuvEventLoop& operator=(LibuvEventLoop&& other) noexcept = delete;

    // --- Base EventLoop Implementation ---
    absl::Status run() override;
    std::future<absl::Status> shutdown(std::chrono::milliseconds timeout) override;
    void stop() override;
    bool isOnLoopThread() const override;
    void* getRawLoop() const override;

    // --- Task Posting and Scheduling ---
    absl::Status post(Task task) override;
    absl::Status post(Task task, std::chrono::milliseconds delay) override;

    std::shared_ptr<Timer> scheduleDelayed(Task task, std::chrono::milliseconds delay) override;

    std::shared_ptr<Timer> scheduleRepeating(Task task, std::chrono::milliseconds initial_delay,
                                             std::chrono::milliseconds interval) override;

  private:
    friend class LibuvTimer;
    void processTasks();
    absl::Status doPost(Task task);

    /// The core libuv event loop instance.
    uv_loop_t* mLoop = nullptr;
    /// A libuv idle handle that keeps the loop from exiting when idle.
    uv_idle_t* mKeepAliveHandle = nullptr;
    /// A libuv async handle used to wake up the loop thread to process tasks.
    uv_async_t mAsyncHandle;

    /// The thread ID of the thread currently running the event loop.
    std::thread::id mThreadId;

    /// Mutex protecting access to the mActiveTimers set.
    absl::Mutex mActiveTimersMutex;
    /// A set of raw pointers to all active timers for tracking during shutdown.
    absl::flat_hash_set<Timer*> mActiveTimers ABSL_GUARDED_BY(mActiveTimersMutex);

    /// Mutex protecting access to the mTaskQueue.
    absl::Mutex mTaskMutex;
    /// Queue of tasks posted from external threads to be run on the loop.
    std::queue<Task> mTaskQueue ABSL_GUARDED_BY(mTaskMutex);

    /// Atomic flag indicating the loop is shutting down and will not accept new tasks.
    std::atomic<bool> mIsShuttingDown{false};
    std::promise<absl::Status> mShutdownCompletePromise;
    std::atomic<bool> mPromiseSet{false};
    std::atomic<bool> mIsRunning{false};
};

}  // namespace goldfish::async