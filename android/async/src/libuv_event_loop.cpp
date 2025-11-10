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
    absl::Status run() override;

    // --- Base EventLoop Implementation ---
    std::future<absl::Status> shutdown() override;

    bool isOnLoopThread() const override { return std::this_thread::get_id() == mThreadId; }

    void* getRawLoop() override { return &mUvLoopHandle; }

    // --- Task Posting and Scheduling ---
    std::shared_ptr<EventLoop::Timer> createTimer(Task task) override;

  private:
    friend class LibuvTimer;

    void addActiveTimer(const std::shared_ptr<LibuvTimer>& t) {
        LOG_IF(DFATAL, !isOnLoopThread()) << "addActiveTimer must be called from the loop thread";
        std::weak_ptr<LibuvTimer>& existing = mActiveTimers[t.get()];
        assert(existing.expired() && "Tried to insert a duplicate timer");
        existing = t;
    }

    void removeActiveTimer(LibuvTimer* const t) {
        LOG_IF(DFATAL, !isOnLoopThread()) << "removeActiveTimer must be called from the loop thread";
        const size_t erased = mActiveTimers.erase(t);
        assert((erased == 1) && "Tried to remove a timer that didn't exist");
    }

    void shutdownTimers();

    absl::Status postDelayed(Task task, std::chrono::milliseconds delay) override;

    absl::Status postImmediately(Task task) override {
        if (mIsShuttingDown) {
            LOG(WARNING) << "LibuvEventLoopImpl is not available as it is shutting down";
            return absl::UnavailableError("LibuvEventLoopImpl is shutting down");
        }
        postImmediatelyInternal(std::move(task));
        return absl::OkStatus();
    }

    void postImmediatelyInternal(Task task);

    void processTasks();

    uv_async_t* getAsync() { return mAsyncHandleValid.load() ? &mAsyncHandle : nullptr; }

    uv_async_t* takeOwnershipAsync() {
        return mAsyncHandleValid.exchange(false) ? &mAsyncHandle : nullptr;
    }

    /// The core libuv event loop instance.
    uv_loop_t mUvLoopHandle;

    /// A libuv async handle used to wake up the loop thread to process tasks.
    uv_async_t mAsyncHandle;
    std::atomic<bool> mAsyncHandleValid = false;

    /// The thread ID of the thread currently running the event loop.
    std::atomic<std::thread::id> mThreadId;

    // A map of raw pointers to their corresponding weak pointers for safe shutdown.
    // Must be accessed only from the loop.
    absl::flat_hash_map<LibuvTimer*, std::weak_ptr<LibuvTimer>> mActiveTimers;

    /// Mutex protecting access to the mTaskQueue.
    absl::Mutex mTaskMutex;
    /// Queue of tasks posted from external threads to be run on the loop.
    std::queue<Task> mTaskQueue ABSL_GUARDED_BY(mTaskMutex);

    /// Atomic flag indicating the loop is shutting down and will not accept new tasks.
    std::atomic<bool> mIsShuttingDown{false};
    std::promise<absl::Status> mShutdownCompletePromise;
    std::atomic<bool> mPromiseSet{false};
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
    static std::shared_ptr<LibuvTimer> create(LibuvEventLoopImpl* loop, EventLoop::Task task,
                                              bool auto_cancel) {
        auto timer = std::make_shared<LibuvTimer>(loop, std::move(task), auto_cancel, Private());
        timer->addItselfToActiveTimers();
        return timer;
    }

    static void unpinItselfOnClose(uv_handle_t* handle) {
        assert(handle);
        uv_timer_t* uvTimer = reinterpret_cast<uv_timer_t*>(handle);
        assert(uvTimer->data);
        LibuvTimer* that = static_cast<LibuvTimer*>(uvTimer->data);

        // We get here from `uv_close`, see `takeOwnershipUvTimer`
        assert(!that->mUvTimerHandleValid.load());
        that->mEventLoop.load()->removeActiveTimer(that);
        that->mEventLoop.store(nullptr);
        that->mPinnedByUvTimer.reset();  // potentially calls ~LibuvTimer
    }

    LibuvTimer(LibuvEventLoopImpl* loop, EventLoop::Task task, bool auto_cancel, Private)
            : mEventLoop(loop), mTask(std::move(task)), mAutoCancel(auto_cancel) {}

    ~LibuvTimer() override {}

    void doCancel() {
        if (uv_timer_t* uvTimer = takeOwnershipUvTimer()) {
            uv_timer_stop(uvTimer);
            uv_close(reinterpret_cast<uv_handle_t*>(uvTimer), unpinItselfOnClose);
        }
    }

    void cancel() override {
        if (auto *loop = mEventLoop.load()) {
            // Stop and delete the timer from the event loop.
            loop->postImmediatelyInternal([self = shared_from_this()]() { self->doCancel(); });
        } else {
            LOG(WARNING) << "Trying to cancel a timer that's already been cancelled";
        }
    }

    void schedule(std::chrono::milliseconds new_delay,
                             std::chrono::milliseconds new_interval) override {
        if (auto *loop = mEventLoop.load()) {
            loop->postImmediatelyInternal([self = shared_from_this(), new_delay_ms = new_delay.count(), new_interval_ms = new_interval.count()] {
                if (uv_timer_t* uvTimer = self->getUvTimer()) {
                    uv_timer_stop(uvTimer);
                    uv_timer_start(uvTimer, onTimer, new_delay_ms, new_interval_ms);
                }
            });
        } else {
            LOG(WARNING) << "Trying to schedule a timer that's been cancelled";
        }
    }

  private:
    void addItselfToActiveTimers() {
        // shared_from_this() is not available in the ctor
        auto *loop = mEventLoop.load();
        loop->postImmediatelyInternal([loop, self = shared_from_this()]() {
            assert(!self->mPinnedByUvTimer);
            self->mPinnedByUvTimer = self;
            if (const int err = uv_timer_init(&loop->mUvLoopHandle, &self->mUvTimerHandle)) {
                LOG(DFATAL) << "uv_timer_init failed with: " << uv_strerror(err);
            }
            self->mUvTimerHandle.data = self.get();
            assert(!self->mUvTimerHandleValid.load());
            self->mUvTimerHandleValid.store(true);

            loop->addActiveTimer(self);
        });
    }

    static void onTimer(uv_timer_t* handle) {
        assert(handle->data);
        const auto self = static_cast<LibuvTimer*>(handle->data)->mPinnedByUvTimer;
        assert(self && "onTimer callback is called without a shared_from_this pointer");
        assert(self->mEventLoop.load()->isOnLoopThread() &&
               "onTimer callback is not called from the event loop");

        self->mTask();

        // For one-shot timers, close the handle after execution.
        // This will lead to the object being deleted if the user has
        // also released their shared_ptr.
        if (self->mAutoCancel) {
            self->doCancel();
        }
    }

    uv_timer_t* getUvTimer() { return mUvTimerHandleValid.load() ? &mUvTimerHandle : nullptr; }

    uv_timer_t* takeOwnershipUvTimer() {
        return mUvTimerHandleValid.exchange(false) ? &mUvTimerHandle : nullptr;
    }

    std::atomic<LibuvEventLoopImpl*> mEventLoop;
    EventLoop::Task mTask;
    bool mAutoCancel = false;

    uv_timer_t mUvTimerHandle;
    std::atomic<bool> mUvTimerHandleValid = false;

    std::shared_ptr<LibuvTimer> mPinnedByUvTimer;  ///< prevents calling the dctor
};

// --- LibuvEventLoopImpl Implementation ---

LibuvEventLoopImpl::LibuvEventLoopImpl() {
    if (const int err = uv_loop_init(&mUvLoopHandle)) {
        LOG(DFATAL) << "Failed to initialize uv_loop: " << uv_strerror(err);
    }
    mUvLoopHandle.data = this;
}

LibuvEventLoopImpl::~LibuvEventLoopImpl() {
    LOG_IF(FATAL, getState() != LooperStatusEvent::State::NOT_STARTED && !mIsShuttingDown) << "Uv loop has not been shutdown prior to destruction";
    assert(mActiveTimers.empty());

    int res = uv_loop_close(&mUvLoopHandle);
    if (res != 0) {
        LOG(WARNING) << "Failed to close uv_loop: " << uv_strerror(res);
        if (VLOG_IS_ON(1)) {
            // Note it is expected that the launcher will "leak" handles of any detached processes.
            LOG(WARNING) << "The following handles were leaked:";
            uv_print_all_handles(&mUvLoopHandle, stderr);
        }
    }
}

void LibuvEventLoopImpl::shutdownTimers() {
    LOG_IF(DFATAL, !isOnLoopThread()) << "shutdownTimers must be called from the loop thread";
    // Iterate a copy as doCancel calls back to removeActiveTimer which calls erase.
    auto copy = mActiveTimers;
    for (const auto& [unsafePtr, weakTimer] : copy) {
        if (std::shared_ptr<LibuvTimer> timer = weakTimer.lock()) {
            timer->doCancel();
        } else {
            // Note that we don't expect a timer to have been deleted without first calling
            // removeActiveTimer so "this should never happen"
        }
    }
}

std::shared_ptr<EventLoop::Timer> LibuvEventLoopImpl::createTimer(Task task) {
    if (mIsShuttingDown) {
        return std::make_shared<ScopedTimer>(nullptr);
    }
    auto timer = LibuvTimer::create(this, std::move(task), /*auto_cancel=*/false);
    return std::make_shared<ScopedTimer>(timer);
}

absl::Status LibuvEventLoopImpl::postDelayed(Task task, std::chrono::milliseconds delay) {
    if (mIsShuttingDown) {
        LOG(WARNING) << "LibuvEventLoopImpl is not available as it is shutting down";
        return absl::UnavailableError("LibuvEventLoopImpl is shutting down");
    }
    // For fire-and-forget, the timer's lifetime is managed by its own async
    // operations. We create it and immediately let go of the handle.
    auto timer = LibuvTimer::create(this, std::move(task), /*auto_cancel=*/true);
    timer->schedule(delay, std::chrono::milliseconds::zero());
    return absl::OkStatus();
}

void LibuvEventLoopImpl::postImmediatelyInternal(Task task) {
    // We allow tasks to be queued before the loop has been started.
    {
        absl::MutexLock lock(&mTaskMutex);
        mTaskQueue.push(std::move(task));
    }
    if (uv_async_t* uvAsync = getAsync()) {
        uv_async_send(uvAsync);
    }
}

void LibuvEventLoopImpl::processTasks() {
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

absl::Status LibuvEventLoopImpl::run() {
    mThreadId = std::this_thread::get_id();

    mAsyncHandle.data = this;
    uv_async_init(&mUvLoopHandle, &mAsyncHandle, [](uv_async_t* handle) {
        static_cast<LibuvEventLoopImpl*>(handle->data)->processTasks();
    });
    mAsyncHandleValid.store(true);

    // Post a task to our own queue. When this task executes, we can be
    // certain that the event loop is actively processing events.
    postImmediatelyInternal([this]() { setState(LooperStatusEvent::State::RUNNING); });

    int err = uv_run(&mUvLoopHandle, UV_RUN_DEFAULT);

    setState(LooperStatusEvent::State::FINISHED);
    auto status = UvErrToAbslStatus(err);
    if (!mPromiseSet.exchange(true)) {
        mShutdownCompletePromise.set_value(status);
    }
    return status;
}

std::future<absl::Status> LibuvEventLoopImpl::shutdown() {
    std::promise<absl::Status> promise;
    if (getState() != LooperStatusEvent::State::RUNNING) {
        promise.set_value(absl::InvalidArgumentError("You cannot shutdown a loop that is not running."));
        return promise.get_future();
    }

    if (isOnLoopThread()) {
        promise.set_value(absl::InvalidArgumentError("You cannot shutdown an event loop from the loop thread."));
        return promise.get_future();
    }

    if (mIsShuttingDown.exchange(true)) {
        promise.set_value(absl::InvalidArgumentError("This loop has already been shutdown"));
        return promise.get_future();
    }
    setState(LooperStatusEvent::State::SHUTTING_DOWN);

    // Post the actual shutdown logic using the private postImmediately.
    postImmediatelyInternal([this]() {
        shutdownTimers();

        if (uv_async_t* uvAsync = takeOwnershipAsync()) {
            uv_close((uv_handle_t*)uvAsync, [](uv_handle_t* handle) {
                auto *self = static_cast<LibuvEventLoopImpl*>(handle->data);
                uv_stop(&self->mUvLoopHandle);
                // We expect this to be sent by uv_run's return value (see run() below).
                if (!self->mPromiseSet.exchange(true)) {
                    self->mShutdownCompletePromise.set_value(absl::OkStatus());
                }
            });
        }
    });

    return mShutdownCompletePromise.get_future();
}

std::unique_ptr<LibuvEventLoop> LibuvEventLoop::create() {
    return std::make_unique<LibuvEventLoopImpl>();
}

}  // namespace goldfish::async
