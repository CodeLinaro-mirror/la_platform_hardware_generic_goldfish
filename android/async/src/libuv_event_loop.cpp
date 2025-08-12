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

#include <goldfish/async/uv_to_absl.h>

#include <atomic>
#include <future>
#include <memory>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/time/time.h"

#include "goldfish/async/scoped_async_timer.h"

namespace goldfish::async {

// The timer now inherits from std::enable_shared_from_this to safely manage
// its lifecycle across the user handle and async callbacks.
//
// Handles should always be closed in the onclose callback
// We place shared_from_this in the .data handle in the event queue
class LibuvTimer : public EventLoop::Timer, public std::enable_shared_from_this<LibuvTimer> {
  public:
    // Factory function to ensure proper std::shared_ptr creation.
    // this sets up the mUvTimer with a self reference so it can be
    // placed in a queue.
    static std::shared_ptr<LibuvTimer> create(LibuvEventLoop* loop, EventLoop::Task task,
                                              bool repeating) {
        auto timer = std::make_shared<LibuvTimer>(loop, std::move(task), repeating);
        timer->mUvTimer->data = new std::shared_ptr<LibuvTimer>(timer);
        return timer;
    }

    static void deleteSharedPtrOnClose(uv_handle_t* handle) {
        auto self_shared_ptr = static_cast<std::shared_ptr<LibuvTimer>*>(handle->data);
        delete self_shared_ptr;
        delete handle;
    }

    LibuvTimer(LibuvEventLoop* loop, EventLoop::Task task, bool repeating)
            : mEventLoop(loop), mTask(std::move(task)), mIsRepeating(repeating) {
        mUvTimer = new uv_timer_t;
        mEventLoop->post([uv_timer = mUvTimer, loop = mEventLoop->mLoop]() {
            uv_timer_init(loop, uv_timer);
        });
        absl::MutexLock lock(&mEventLoop->mActiveTimersMutex);
        mEventLoop->mActiveTimers.insert(this);
    }

    ~LibuvTimer() override {
        // We are no longer outstanding..
        {
            absl::MutexLock lock(&mEventLoop->mActiveTimersMutex);
            auto before = mEventLoop->mActiveTimers.size();
            mEventLoop->mActiveTimers.erase(this);
            auto after = mEventLoop->mActiveTimers.size();

            assert(before - after == 1 && "Tried to remove a timer that didn't exist");
        }
        if (!mIsClosed.load()) {
            // We are not closed, this means we still exist on the uv queue and
            // must stop and clean our handle
            mEventLoop->post([uv_timer = mUvTimer]() {
                uv_timer_stop(uv_timer);
                uv_close((uv_handle_t*)uv_timer, [](auto handle) { delete handle; });
            });
        }
    }

    void start(uint64_t timeout_ms, uint64_t repeat_ms) {
        // Post the start operation to the eventloop, at this point
        auto status = mEventLoop->post([self = shared_from_this(), timeout_ms, repeat_ms]() {
            assert(!self->mIsClosed.load() &&
                   "Timer was closed before it started, this should not be possible");
            uv_timer_start(self->mUvTimer, onTimer, timeout_ms, repeat_ms);
        });
        assert(status.ok());
    }

    void doCancel() {
        if (!mIsClosed.exchange(true)) {
            uv_timer_stop(mUvTimer);
            uv_close((uv_handle_t*)mUvTimer, deleteSharedPtrOnClose);
        }
    }

    void cancel() override {
        if (!mIsClosed.load()) {
            // Stop and delete the timer from the event loop.
            mEventLoop->post([self = shared_from_this()]() { self->doCancel(); });
        }
    }

  private:
    static void onTimer(uv_timer_t* handle) {
        auto self_shared_ptr = static_cast<std::shared_ptr<LibuvTimer>*>(handle->data);
        auto self_ptr = *self_shared_ptr;
        assert(self_ptr && "onTimer callback is called without a shared_from_this pointer");
        assert(self_ptr->mEventLoop->isOnLoopThread() &&
               "onTimer callback is not called from the event loop");
        if (!self_ptr->mIsClosed.load()) {
            auto start = absl::Now();

            // Invoke the callback
            self_ptr->mTask();

            // Check for duration and update the cached uvloop time if needed
            auto end = absl::Now();
            auto elapsed = end - start;
            if (elapsed > absl::Milliseconds(1)) {
                // Update the libuv loop's time if the task took longer than 1ms
                VLOG(1) << "Task took " << elapsed << ", updating uv time";
                uv_update_time(self_ptr->mEventLoop->mLoop);
            }

            // For one-shot timers, close the handle after execution.
            // This will lead to the object being deleted if the user has
            // also released their shared_ptr.
            if (!self_ptr->mIsRepeating) {
                if (!self_ptr->mIsClosed.exchange(true)) {
                    uv_close((uv_handle_t*)handle, deleteSharedPtrOnClose);
                }
            }
        }
    }

    LibuvEventLoop* mEventLoop;  ///< The eventloop on which we are scheduled.
    uv_timer_t* mUvTimer;  ///< Handle to the actual timer, ->data contains a shared_from_this()
    EventLoop::Task mTask;
    bool mIsRepeating;
    std::atomic<bool> mIsClosed{false};  ///< True if a uv_close has been scheduled.
};

// --- LibuvEventLoop Implementation ---

LibuvEventLoop::LibuvEventLoop() {
    mLoop = new uv_loop_t();
    int err = uv_loop_init(mLoop);
    if (err != 0) {
        // TODO:  Use factory pattern, so we can guarantee mLoop != nullptr.
        LOG(ERROR) << "Failed to initialize uv_loop: " << uv_strerror(err);
        delete mLoop;
        mLoop = nullptr;
        return;
    }
    mLoop->data = this;

    mKeepAliveHandle = new uv_idle_t();
    uv_idle_init(mLoop, mKeepAliveHandle);
    uv_idle_start(mKeepAliveHandle, [](uv_idle_t* handle) { /* No-op */ });

    mAsyncHandle.data = this;
    uv_async_init(mLoop, &mAsyncHandle, [](uv_async_t* handle) {
        static_cast<LibuvEventLoop*>(handle->data)->processTasks();
    });
}

LibuvEventLoop::~LibuvEventLoop() {
    if (!mLoop) return;

    // Attempt to process any remaining events. This is not guaranteed to
    // fully clean up if shutdown() was not called.
    uv_run(mLoop, UV_RUN_NOWAIT);

    int res = uv_loop_close(mLoop);
    if (res != 0) {
        LOG(WARNING) << "Failed to close uv_loop: " << uv_strerror(res);
        if (mIsShuttingDown) {
            LOG(WARNING) << "Shutdown was not called!";
        }
        LOG(WARNING) << "The following handles were leaked:";
        uv_print_all_handles(mLoop, stderr);
    }

    // Clean up the memory for the handle structures themselves.
    delete mKeepAliveHandle;
    delete mLoop;
}

/**
 * @brief A context structure to manage the state of the shutdown operation.
 *
 * Since the shutdown process is asynchronous and involves multiple callbacks,
 * we need a way to share state between them. This struct is created on the heap
 * and holds the promise to be fulfilled and a counter for tracking pending
 * handle closures. A pointer to this context is passed to the callbacks via
 * the `uv_handle_t::data` field.
 */
struct ShutdownContext {
    std::shared_ptr<std::promise<absl::Status>> promise;
    std::atomic<int> handles_to_close;
};

/**
 * @brief A static C-style callback for when internal handles are closed.
 *
 * This function has the signature required by `uv_close`. It retrieves the
 * shared ShutdownContext from the handle's `data` pointer.
 *
 * The logic implements a "last one out cleans up" pattern: each callback
 * decrements the atomic counter, but only the final callback (when the
 * counter reaches zero) fulfills the promise and deletes the heap-allocated
 * context.
 *
 * @param handle The libuv handle that has just been closed.
 */
static void onInternalHandleClosed(uv_handle_t* handle) {
    auto* context = static_cast<ShutdownContext*>(handle->data);
    assert(context && "No contex present in onInternalHandleClosed");

    auto open_handles = --(context->handles_to_close);
    // Atomically decrement the counter, the last will cleanup
    if (open_handles == 0) {
        // This is the last handle to close, so we can now safely signal
        // that the shutdown operation is complete and delete the context
        context->promise->set_value(absl::OkStatus());
        delete context;
    }
}

std::future<absl::Status> LibuvEventLoop::shutdown(std::chrono::milliseconds timeout) {
    auto wait_until = absl::Now() + absl::FromChrono(timeout);

    // Create a promise to signal when shutdown is complete.
    // It's wrapped in a shared_ptr to be safely passed to the context.
    auto promise = std::make_shared<std::promise<absl::Status>>();
    std::future<absl::Status> future = promise->get_future();

    if (!mIsRunning) {
        promise->set_value(absl::InternalError("You cannot shutdown a loop that is not running."));
        return future;
    }

    if (mIsShuttingDown.load()) {
        promise->set_value(absl::InvalidArgumentError("This loop has already been shutdown"));
        return future;
    }

    // First we cancel all outstanding timers.
    if (isOnLoopThread()) {
        promise->set_value(absl::InvalidArgumentError(
                "You cannot shutdown an event loop from the loop thread."));
        return future;
    }

    // Post the actual shutdown logic to the event loop thread. This ensures
    // all interactions with libuv handles happen on the correct thread.
    auto status = post([this, promise, wait_until]() {
        // The post queue is now offically closed.
        mIsShuttingDown.store(true);

        {
            absl::MutexLock lock(&mActiveTimersMutex);
            for (auto timer : mActiveTimers) {
                static_cast<LibuvTimer*>(timer)->doCancel();

                // Check if we are past our deadline.
                if (absl::Now() > wait_until) {
                    promise->set_value(absl::DeadlineExceededError(
                            "Unable to cancel timers in a timely fashion."));
                    return;
                }
            }
        }

        // Create the context on the heap. Its lifetime must persist across
        // multiple loop ticks until all close callbacks have fired.
        // We have 2 internal handles to close: mKeepAliveHandle and mAsyncHandle.
        auto* context = new ShutdownContext{promise, 2};

        // Attach the shared context to each handle. This is how the static
        // callback will retrieve the state it needs to operate on.
        // Note that at this point we are overriding the async handle
        // and you can no longer post to the queue.
        mKeepAliveHandle->data = context;
        mAsyncHandle.data = context;

        // Schedule the closing of both internal handles. Libuv will call
        // onInternalHandleClosed for each one when they are fully closed.
        uv_close((uv_handle_t*)mKeepAliveHandle, onInternalHandleClosed);
        uv_close((uv_handle_t*)&mAsyncHandle, onInternalHandleClosed);
    });
    return future;
}

absl::Status LibuvEventLoop::run() {
    if (!mLoop) {
        return absl::UnavailableError("Event loop is not initialized.");
    }

    mThreadId = std::this_thread::get_id();
    mIsRunning = true;
    int err = uv_run(mLoop, UV_RUN_DEFAULT);
    mIsRunning = false;
    auto status = UvErrToAbslStatus(err);
    err = uv_idle_stop(mKeepAliveHandle);
    if (status.ok()) {
        status = UvErrToAbslStatus(err);
    }
    return status;
}

void LibuvEventLoop::stop() {
    if (mLoop) {
        if (!mIsShuttingDown) {
            LOG(WARNING) << "The event loop is stopping without a call to shutdown! You will leak "
                            "handles.";
        }
        uv_stop(mLoop);
    }
}

bool LibuvEventLoop::isOnLoopThread() const {
    return mLoop && (std::this_thread::get_id() == mThreadId);
}

void LibuvEventLoop::processTasks() {
    std::queue<Task> tasks;
    {
        absl::MutexLock lock(&mTaskMutex);
        tasks.swap(mTaskQueue);
    }
    while (!tasks.empty()) {
        tasks.front()();
        tasks.pop();
    }
}

absl::Status LibuvEventLoop::post(Task task) {
    if (!mLoop) return absl::UnavailableError("Event loop is not initialized.");
    if (mIsShuttingDown.load()) {
        return absl::CancelledError("Event loop is shutting down.");
    }
    {
        absl::MutexLock lock(&mTaskMutex);
        mTaskQueue.push(std::move(task));
    }
    uv_async_send(&mAsyncHandle);
    return absl::OkStatus();
}

// Cleaner fire-and-forget implementation.
absl::Status LibuvEventLoop::post(Task task, std::chrono::milliseconds delay) {
    if (!mLoop) return absl::UnavailableError("Event loop is not initialized.");
    // For fire-and-forget, the timer's lifetime is managed by its own async
    // operations. We create it and immediately let go of the handle.
    auto timer = LibuvTimer::create(this, std::move(task), /*repeating=*/false);
    timer->start(delay.count(), 0);
    return absl::OkStatus();
}

std::shared_ptr<EventLoop::Timer> LibuvEventLoop::scheduleDelayed(Task task,
                                                                  std::chrono::milliseconds delay) {
    auto timer = LibuvTimer::create(this, std::move(task), /*repeating=*/false);
    timer->start(delay.count(), 0);
    return std::make_shared<ScopedTimer>(timer);
}

std::shared_ptr<EventLoop::Timer> LibuvEventLoop::scheduleRepeating(
        Task task, std::chrono::milliseconds initial_delay, std::chrono::milliseconds interval) {
    auto timer = LibuvTimer::create(this, std::move(task), /*repeating=*/true);
    timer->start(initial_delay.count(), interval.count());
    return std::make_shared<ScopedTimer>(timer);
}

void* LibuvEventLoop::getRawLoop() const {
    return mLoop;
}

}  // namespace goldfish::async