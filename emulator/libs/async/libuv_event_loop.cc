// Copyright (C) 2025 The Android Open Source Project
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

#include "goldfish/async/libuv_event_loop.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <queue>
#include <thread>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/async/scoped_async_timer.h"
#include "goldfish/async/uv_to_absl.h"
#include "uv.h"

namespace goldfish::async {

class LibuvTimer;

/**
 * @brief A concrete implementation of the EventLoop interface using libuv.
 * @class LibuvEventLoopImpl
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
 * `LibuvTimer` pins itself via `mPinnedByUvTimer` which is unpinned in
 * `unpinItselfOnClose`.
 */
class LibuvEventLoopImpl : public LibuvEventLoop {
  public:
    LibuvEventLoopImpl();
    ~LibuvEventLoopImpl() override;

    // --- Prevent Copying ---
    LibuvEventLoopImpl(const LibuvEventLoopImpl&) = delete;
    LibuvEventLoopImpl& operator=(const LibuvEventLoopImpl&) = delete;

    // --- Moving is not supported for simplicity ---
    LibuvEventLoopImpl(LibuvEventLoopImpl&& other) noexcept = delete;
    LibuvEventLoopImpl& operator=(LibuvEventLoopImpl&& other) noexcept = delete;

    // --- LibuvEventLoop Implementation ---
    absl::Status Run() override;

    // --- Base EventLoop Implementation ---
    std::future<absl::Status> Shutdown() override;

    bool IsOnLoopThread() const override { return std::this_thread::get_id() == thread_id_; }

    void* GetRawLoop() override { return &uv_loop_handle_; }

    // --- Task Posting and Scheduling ---
    std::shared_ptr<EventLoop::Timer> CreateTimer(Task task) override;

  private:
    friend class LibuvTimer;

    void AddActiveTimer(const std::shared_ptr<LibuvTimer>& t) {
        LOG_IF(DFATAL, !IsOnLoopThread()) << "addActiveTimer must be called from the loop thread";
        std::weak_ptr<LibuvTimer>& existing = active_timers_[t.get()];
        DCHECK(existing.expired()) << "Tried to insert a duplicate timer that was already active.";
        existing = t;
    }

    void RemoveActiveTimer(LibuvTimer* const t) {
        LOG_IF(DFATAL, !IsOnLoopThread())
                << "removeActiveTimer must be called from the loop thread";
        const size_t erased = active_timers_.erase(t);
        DCHECK(erased == 1)
                << "Tried to remove a timer that didn't exist in the active timers set.";
    }

    void ShutdownTimers();

    absl::Status PostDelayed(Task task, std::chrono::milliseconds delay) override;

    absl::Status PostImmediately(Task task) override {
        if (is_shutting_down_) {
            LOG(WARNING) << "LibuvEventLoopImpl is not available as it is shutting down";
            return absl::UnavailableError("LibuvEventLoopImpl is shutting down");
        }
        PostImmediatelyInternal(std::move(task));
        return absl::OkStatus();
    }

    void PostImmediatelyInternal(Task task);

    void ProcessTasks();

    uv_async_t* GetAsync() { return async_handle_valid_.load() ? &async_handle_ : nullptr; }

    uv_async_t* TakeOwnershipAsync() {
        return async_handle_valid_.exchange(false) ? &async_handle_ : nullptr;
    }

    /// The core libuv event loop instance.
    uv_loop_t uv_loop_handle_;

    /// A libuv async handle used to wake up the loop thread to process tasks.
    uv_async_t async_handle_;
    std::atomic<bool> async_handle_valid_ = false;

    /// The thread ID of the thread currently running the event loop.
    std::atomic<std::thread::id> thread_id_;

    // A map of raw pointers to their corresponding weak pointers for safe shutdown.
    // Must be accessed only from the loop.
    absl::flat_hash_map<LibuvTimer*, std::weak_ptr<LibuvTimer>> active_timers_;

    /// Mutex protecting access to the mTaskQueue.
    absl::Mutex task_mutex_;
    /// Queue of tasks posted from external threads to be run on the loop.
    std::queue<Task> task_queue_ ABSL_GUARDED_BY(task_mutex_);

    /// Atomic flag indicating the loop is shutting down and will not accept new tasks.
    std::atomic<bool> is_shutting_down_{false};
    std::promise<absl::Status> shutdown_complete_promise_;
    std::atomic<bool> promise_set_{false};
};

// The timer now inherits from std::enable_shared_from_this to safely manage
// its lifecycle across the user handle and async callbacks.
//
// Handles should always be closed in the onclose callback
// We place shared_from_this in the .data handle in the event queue
class LibuvTimer : public EventLoop::Timer, public std::enable_shared_from_this<LibuvTimer> {
    struct Private {};

  public:
    // Factory function to ensure proper std::shared_ptr creation.
    // this sets up the mUvTimer with a self reference so it can be
    // placed in a queue.
    static std::shared_ptr<LibuvTimer> Create(LibuvEventLoopImpl* loop, EventLoop::Task task,
                                              bool auto_cancel) {
        auto timer = std::make_shared<LibuvTimer>(loop, std::move(task), auto_cancel, Private());
        timer->AddItselfToActiveTimers();
        return timer;
    }

    static void UnpinItselfOnClose(uv_handle_t* handle) {
        DCHECK(handle) << "UV handle cannot be null in close callback.";
        auto* uv_timer = reinterpret_cast<uv_timer_t*>(handle);
        DCHECK(uv_timer->data) << "UV timer handle must have a pointer to the LibuvTimer instance.";
        auto* that = static_cast<LibuvTimer*>(uv_timer->data);

        // We get here from `uv_close`, see `takeOwnershipUvTimer`
        DCHECK(!that->uv_timer_handle_valid_.load())
                << "Timer handle should have been invalidated before closing.";
        that->event_loop_.load()->RemoveActiveTimer(that);
        that->event_loop_.store(nullptr);
        that->pinned_by_uv_timer_.reset();  // potentially calls ~LibuvTimer
    }

    LibuvTimer(LibuvEventLoopImpl* loop, EventLoop::Task task, bool auto_cancel, Private)
            : event_loop_(loop), task_(std::move(task)), auto_cancel_(auto_cancel) {}

    ~LibuvTimer() override = default;

    void DoCancel() {
        if (uv_timer_t* uv_timer = TakeOwnershipUvTimer()) {
            uv_timer_stop(uv_timer);
            uv_close(reinterpret_cast<uv_handle_t*>(uv_timer), UnpinItselfOnClose);
        }
    }

    void Cancel() override {
        if (auto* loop = event_loop_.load()) {
            // Stop and delete the timer from the event loop.
            loop->PostImmediatelyInternal([self = shared_from_this()]() { self->DoCancel(); });
        } else {
            LOG(WARNING) << "Trying to cancel a timer that's already been cancelled";
        }
    }

    void Schedule(std::chrono::milliseconds new_delay,
                  std::chrono::milliseconds new_interval) override {
        if (auto* loop = event_loop_.load()) {
            loop->PostImmediatelyInternal([self = shared_from_this(),
                                           new_delay_ms = new_delay.count(),
                                           new_interval_ms = new_interval.count()] {
                if (uv_timer_t* uv_timer = self->GetUvTimer()) {
                    uv_timer_stop(uv_timer);
                    uv_timer_start(uv_timer, OnTimer, new_delay_ms, new_interval_ms);
                }
            });
        } else {
            LOG(WARNING) << "Trying to schedule a timer that's been cancelled";
        }
    }

  private:
    void AddItselfToActiveTimers() {
        // shared_from_this() is not available in the ctor
        auto* loop = event_loop_.load();
        loop->PostImmediatelyInternal([loop, self = shared_from_this()]() {
            DCHECK(!self->pinned_by_uv_timer_)
                    << "Timer should not be pinned before initialization.";
            self->pinned_by_uv_timer_ = self;
            if (const int err = uv_timer_init(&loop->uv_loop_handle_, &self->uv_timer_handle_)) {
                LOG(DFATAL) << "uv_timer_init failed with: " << uv_strerror(err);
            }
            self->uv_timer_handle_.data = self.get();
            DCHECK(!self->uv_timer_handle_valid_.load())
                    << "Timer handle should be invalid before initialization.";
            self->uv_timer_handle_valid_.store(true);

            loop->AddActiveTimer(self);
        });
    }

    static void OnTimer(uv_timer_t* handle) {
        DCHECK(handle->data) << "UV timer handle must have a pointer to the LibuvTimer instance.";
        const auto self = static_cast<LibuvTimer*>(handle->data)->pinned_by_uv_timer_;
        DCHECK(self) << "onTimer callback called without a valid LibuvTimer instance.";
        DCHECK(self->event_loop_.load()->IsOnLoopThread())
                << "onTimer callback must be executed on the event loop thread.";

        self->task_();

        // For one-shot timers, close the handle after execution.
        // This will lead to the object being deleted if the user has
        // also released their shared_ptr.
        if (self->auto_cancel_) {
            self->DoCancel();
        }
    }

    uv_timer_t* GetUvTimer() { return uv_timer_handle_valid_.load() ? &uv_timer_handle_ : nullptr; }

    uv_timer_t* TakeOwnershipUvTimer() {
        return uv_timer_handle_valid_.exchange(false) ? &uv_timer_handle_ : nullptr;
    }

    std::atomic<LibuvEventLoopImpl*> event_loop_;
    EventLoop::Task task_;
    bool auto_cancel_ = false;

    uv_timer_t uv_timer_handle_;
    std::atomic<bool> uv_timer_handle_valid_ = false;

    std::shared_ptr<LibuvTimer> pinned_by_uv_timer_;  ///< prevents calling the dctor
};

// --- LibuvEventLoopImpl Implementation ---

LibuvEventLoopImpl::LibuvEventLoopImpl() {
    if (const int err = uv_loop_init(&uv_loop_handle_)) {
        LOG(DFATAL) << "Failed to initialize uv_loop: " << uv_strerror(err);
    }
    uv_loop_handle_.data = this;

    async_handle_.data = this;
    uv_async_init(&uv_loop_handle_, &async_handle_, [](uv_async_t* handle) {
        static_cast<LibuvEventLoopImpl*>(handle->data)->ProcessTasks();
    });
    async_handle_valid_.store(true);
}

LibuvEventLoopImpl::~LibuvEventLoopImpl() {
    // NOLINTNEXTLINE(readability-simplify-boolean-expr)
    LOG_IF(FATAL, GetState() != LooperStatusEvent::State::kNotStarted && !is_shutting_down_)
            << "Uv loop has not been shutdown prior to destruction";
    DCHECK(active_timers_.empty())
            << "All timers should have been cancelled and removed before the "
               "event loop is destroyed.";

    // Last ditch effort to close the wakeup handle if the loop was never shut down.
    if (uv_async_t* async = TakeOwnershipAsync()) {
        uv_close(reinterpret_cast<uv_handle_t*>(async), nullptr);
        // Process the close callback immediately.
        uv_run(&uv_loop_handle_, UV_RUN_NOWAIT);
    }

    const int res = uv_loop_close(&uv_loop_handle_);
    if (res != 0) {
        LOG(WARNING) << "Failed to close uv_loop: " << uv_strerror(res);
        if (VLOG_IS_ON(1)) {
            // Note it is expected that the launcher will "leak" handles of any detached processes.
            LOG(WARNING) << "The following handles were leaked:";
            uv_print_all_handles(&uv_loop_handle_, stderr);
        }
    }
}

void LibuvEventLoopImpl::ShutdownTimers() {
    LOG_IF(DFATAL, !IsOnLoopThread()) << "shutdownTimers must be called from the loop thread";
    // Iterate a copy as doCancel calls back to removeActiveTimer which calls erase.
    auto copy = active_timers_;
    for (const auto& [unsafePtr, weakTimer] : copy) {
        if (const std::shared_ptr<LibuvTimer> timer = weakTimer.lock()) {
            timer->DoCancel();
        } else {
            // Note that we don't expect a timer to have been deleted without first calling
            // removeActiveTimer so "this should never happen"
        }
    }
}

std::shared_ptr<EventLoop::Timer> LibuvEventLoopImpl::CreateTimer(Task task) {
    if (is_shutting_down_) {
        return std::make_shared<ScopedTimer>(nullptr);
    }
    auto timer = LibuvTimer::Create(this, std::move(task), /*auto_cancel=*/false);
    return std::make_shared<ScopedTimer>(timer);
}

absl::Status LibuvEventLoopImpl::PostDelayed(Task task, std::chrono::milliseconds delay) {
    if (is_shutting_down_) {
        LOG(WARNING) << "LibuvEventLoopImpl is not available as it is shutting down";
        return absl::UnavailableError("LibuvEventLoopImpl is shutting down");
    }
    // For fire-and-forget, the timer's lifetime is managed by its own async
    // operations. We create it and immediately let go of the handle.
    auto timer = LibuvTimer::Create(this, std::move(task), /*auto_cancel=*/true);
    timer->Schedule(delay, std::chrono::milliseconds::zero());
    return absl::OkStatus();
}

void LibuvEventLoopImpl::PostImmediatelyInternal(Task task) {
    // We allow tasks to be queued before the loop has been started.
    {
        const absl::MutexLock lock(task_mutex_);
        task_queue_.push(std::move(task));
    }
    if (uv_async_t* uv_async = GetAsync()) {
        uv_async_send(uv_async);
    }
}

void LibuvEventLoopImpl::ProcessTasks() {
    std::queue<Task> tasks;
    {
        const absl::MutexLock lock(task_mutex_);
        tasks.swap(task_queue_);
    }
    while (!tasks.empty()) {
        tasks.front()();
        tasks.pop();
    }
}

absl::Status LibuvEventLoopImpl::Run() {
    thread_id_ = std::this_thread::get_id();

    SetState(LooperStatusEvent::State::kRunning);
    const int err = uv_run(&uv_loop_handle_, UV_RUN_DEFAULT);
    SetState(LooperStatusEvent::State::kFinished);

    auto status = UvErrToAbslStatus(err);
    if (!promise_set_.exchange(true)) {
        shutdown_complete_promise_.set_value(status);
    }
    return status;
}

std::future<absl::Status> LibuvEventLoopImpl::Shutdown() {
    std::promise<absl::Status> promise;
    if (GetState() != LooperStatusEvent::State::kRunning) {
        promise.set_value(
                absl::InvalidArgumentError("You cannot shutdown a loop that is not running."));
        return promise.get_future();
    }

    if (IsOnLoopThread()) {
        promise.set_value(absl::InvalidArgumentError(
                "You cannot shutdown an event loop from the loop thread."));
        return promise.get_future();
    }

    if (is_shutting_down_.exchange(true)) {
        promise.set_value(absl::InvalidArgumentError("This loop has already been shutdown"));
        return promise.get_future();
    }
    SetState(LooperStatusEvent::State::kShuttingDown);

    // Post the actual shutdown logic using the private postImmediately.
    PostImmediatelyInternal([this]() {
        ShutdownTimers();

        if (uv_async_t* uv_async = TakeOwnershipAsync()) {
            uv_close(reinterpret_cast<uv_handle_t*>(uv_async), [](uv_handle_t* handle) {
                auto* self = static_cast<LibuvEventLoopImpl*>(handle->data);
                uv_stop(&self->uv_loop_handle_);
                // We expect this to be sent by uv_run's return value (see run() below).
                if (!self->promise_set_.exchange(true)) {
                    self->shutdown_complete_promise_.set_value(absl::OkStatus());
                }
            });
        }
    });

    return shutdown_complete_promise_.get_future();
}

std::unique_ptr<LibuvEventLoop> LibuvEventLoop::Create() {
    return std::make_unique<LibuvEventLoopImpl>();
}

}  // namespace goldfish::async
