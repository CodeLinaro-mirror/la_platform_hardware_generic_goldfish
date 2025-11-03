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
#include <functional>
#include <future>
#include <memory>
#include <queue>
#include <thread>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

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

    // --- Base EventLoop Implementation ---
    absl::Status run() override;
    std::future<absl::Status> shutdown(std::chrono::milliseconds timeout) override;
    void stop() override;
    bool isOnLoopThread() const override;
    void* getRawLoop() override { return &mUvLoopHandle; }

    // --- Task Posting and Scheduling ---
    void postImpl(Task task, std::chrono::milliseconds delay) override;

    std::shared_ptr<Timer> scheduleDelayed(Task task, std::chrono::milliseconds delay) override;

    std::shared_ptr<Timer> scheduleRepeating(Task task, std::chrono::milliseconds initial_delay,
                                             std::chrono::milliseconds interval) override;

  private:
    friend class LibuvTimer;

    void addActiveTimer(const std::shared_ptr<LibuvTimer>&);
    void removeActiveTimer(LibuvTimer*);

    void processTasks();
    void doPost(Task task);

    /// The core libuv event loop instance.
    uv_loop_t mUvLoopHandle;

    /// A libuv async handle used to wake up the loop thread to process tasks.
    uv_async_t mAsyncHandle;

    /// The thread ID of the thread currently running the event loop.
    std::atomic<std::thread::id> mThreadId;

    /// Mutex protecting access to the mActiveTimers set.
    absl::Mutex mActiveTimersMutex;
    /// A map of raw pointers to their corresponding weak pointers for safe shutdown.
    absl::flat_hash_map<LibuvTimer*, std::weak_ptr<LibuvTimer>>
            mActiveTimers ABSL_GUARDED_BY(mActiveTimersMutex);

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
                                              bool repeating) {
        auto timer = std::make_shared<LibuvTimer>(loop, std::move(task), repeating, Private());
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
        that->mPinnedByUvTimer.reset();  // potentially calls ~LibuvTimer
    }

    LibuvTimer(LibuvEventLoopImpl* loop, EventLoop::Task task, bool repeating, Private)
            : mEventLoop(loop), mTask(std::move(task)), mIsRepeating(repeating) {}

    ~LibuvTimer() override { mEventLoop->removeActiveTimer(this); }

    void start(uint64_t timeout_ms, uint64_t repeat_ms) {
        // Post the start operation to the eventloop, at this point
        mEventLoop->post([self = shared_from_this(), timeout_ms, repeat_ms]() {
            if (uv_timer_t* uvTimer = self->getUvTimer()) {
                uv_timer_start(uvTimer, onTimer, timeout_ms, repeat_ms);
            }
        });
    }

    void doCancel() {
        if (uv_timer_t* uvTimer = takeOwnershipUvTimer()) {
            uv_timer_stop(uvTimer);
            uv_close(reinterpret_cast<uv_handle_t*>(uvTimer), unpinItselfOnClose);
        }
    }

    void cancel() override {
        // Stop and delete the timer from the event loop.
        (void)mEventLoop->post([self = shared_from_this()]() { self->doCancel(); });
    }

    void rescheduleRepeating(std::chrono::milliseconds new_delay,
                             std::chrono::milliseconds new_interval) override {
        (void)mEventLoop->post([self = shared_from_this(), new_delay, new_interval]() {
            if (uv_timer_t* uvTimer = self->getUvTimer()) {
                uv_timer_stop(uvTimer);
                self->mIsRepeating = true;
                uv_timer_start(uvTimer, onTimer, new_delay.count(), new_interval.count());
            }
        });
    }

  private:
    void addItselfToActiveTimers() {
        // shared_from_this() is not available in the ctor
        (void)mEventLoop->post([self = shared_from_this()]() {
            LibuvEventLoopImpl& evLoop = *self->mEventLoop;

            assert(!self->mPinnedByUvTimer);
            self->mPinnedByUvTimer = self;
            if (const int err = uv_timer_init(&evLoop.mUvLoopHandle, &self->mUvTimerHandle)) {
                LOG(DFATAL) << "uv_timer_init failed with: " << uv_strerror(err);
            }
            self->mUvTimerHandle.data = self.get();
            assert(!self->mUvTimerHandleValid.load());
            self->mUvTimerHandleValid.store(true);

            evLoop.addActiveTimer(self);
        });
    }

    static void onTimer(uv_timer_t* handle) {
        assert(handle->data);
        const auto self = static_cast<LibuvTimer*>(handle->data)->mPinnedByUvTimer;
        assert(self && "onTimer callback is called without a shared_from_this pointer");
        assert(self->mEventLoop->isOnLoopThread() &&
               "onTimer callback is not called from the event loop");

        self->mTask();

        // For one-shot timers, close the handle after execution.
        // This will lead to the object being deleted if the user has
        // also released their shared_ptr.
        if (!self->mIsRepeating) {
            if (uv_timer_t* uvTimer = self->takeOwnershipUvTimer()) {
                uv_close(reinterpret_cast<uv_handle_t*>(uvTimer), unpinItselfOnClose);
            }
        }
    }

    uv_timer_t* getUvTimer() { return mUvTimerHandleValid.load() ? &mUvTimerHandle : nullptr; }

    uv_timer_t* takeOwnershipUvTimer() {
        return mUvTimerHandleValid.exchange(false) ? &mUvTimerHandle : nullptr;
    }

    LibuvEventLoopImpl* mEventLoop;  ///< The eventloop on which we are scheduled.
    uv_timer_t mUvTimerHandle;       ///< Handle to the actual timer, `.data` point to `this`
    EventLoop::Task mTask;
    std::shared_ptr<LibuvTimer> mPinnedByUvTimer;  ///< prevents calling the dctor
    std::atomic<bool> mUvTimerHandleValid = false;
    bool mIsRepeating;
};

// --- LibuvEventLoopImpl Implementation ---

LibuvEventLoopImpl::LibuvEventLoopImpl() {
    if (const int err = uv_loop_init(&mUvLoopHandle)) {
        LOG(DFATAL) << "Failed to initialize uv_loop: " << uv_strerror(err);
    }
    mUvLoopHandle.data = this;

    mAsyncHandle.data = this;
    uv_async_init(&mUvLoopHandle, &mAsyncHandle, [](uv_async_t* handle) {
        static_cast<LibuvEventLoopImpl*>(handle->data)->processTasks();
    });
}

LibuvEventLoopImpl::~LibuvEventLoopImpl() {
    // Attempt to process any remaining events. This is not guaranteed to
    // fully clean up if shutdown() was not called.
    uv_run(&mUvLoopHandle, UV_RUN_NOWAIT);

    int res = uv_loop_close(&mUvLoopHandle);
    if (res != 0) {
        LOG(WARNING) << "Failed to close uv_loop: " << uv_strerror(res);
        if (!mIsShuttingDown) {
            LOG(WARNING) << "Shutdown was not called!";
        }
        LOG(WARNING) << "The following handles were leaked:";
        uv_print_all_handles(&mUvLoopHandle, stderr);
    }
}

std::unique_ptr<LibuvEventLoop> LibuvEventLoop::create() {
    return std::make_unique<LibuvEventLoopImpl>();
}

void LibuvEventLoopImpl::addActiveTimer(const std::shared_ptr<LibuvTimer>& t) {
    absl::MutexLock lock(&mActiveTimersMutex);
    std::weak_ptr<LibuvTimer>& existing = mActiveTimers[t.get()];
    assert(existing.expired() && "Tried to insert a duplicate timer");
    existing = t;
}

void LibuvEventLoopImpl::removeActiveTimer(LibuvTimer* const t) {
    absl::MutexLock lock(&mActiveTimersMutex);
    const size_t erased = mActiveTimers.erase(t);
    assert((erased == 1) && "Tried to remove a timer that didn't exist");
}

std::future<absl::Status> LibuvEventLoopImpl::shutdown(std::chrono::milliseconds timeout) {
    bool isRunning = getState() == LooperStatusEvent::State::RUNNING;
    setState(LooperStatusEvent::State::SHUTTING_DOWN);

    // Handle cases where shutdown is not possible by returning an immediately-fulfilled future.
    if (!isRunning || isOnLoopThread()) {
        const char* msg = !isRunning ? "You cannot shutdown a loop that is not running."
                                     : "You cannot shutdown an event loop from the loop thread.";
        std::promise<absl::Status> promise;
        promise.set_value(absl::InvalidArgumentError(msg));
        return promise.get_future();
    }

    // Atomically check and set the shutdown flag. If it was already true, another
    // thread has already started the shutdown process.
    if (mIsShuttingDown.exchange(true)) {
        // Return a new future that is immediately fulfilled with an error.
        // This prevents a crash from trying to get the future from the member
        // promise more than once.
        std::promise<absl::Status> promise;
        promise.set_value(absl::InvalidArgumentError("This loop has already been shutdown"));
        return promise.get_future();
    }

    // --- This is the first and only thread to initiate shutdown ---
    auto wait_until = absl::Now() + absl::FromChrono(timeout);

    // Post the actual shutdown logic using the private doPost.
    (void)doPost([this, wait_until]() {
        {
            // ~LibuvTimer will be called with mActiveTimersMutex unlocked
            std::vector<std::shared_ptr<LibuvTimer>> pinnedTimers;
            {
                absl::MutexLock lock(&mActiveTimersMutex);
                pinnedTimers.reserve(mActiveTimers.size());
                for (const auto& [unsafePtr, weakTimer] : mActiveTimers) {
                    if (std::shared_ptr<LibuvTimer> timer = weakTimer.lock()) {
                        timer->doCancel();
                        pinnedTimers.push_back(std::move(timer));

                        if (absl::Now() > wait_until) {
                            if (!mPromiseSet.exchange(true)) {
                                mShutdownCompletePromise.set_value(absl::DeadlineExceededError(
                                        "Unable to cancel timers in a timely fashion."));
                            }
                            return;
                        }
                    }
                }
            }
        }

        uv_close((uv_handle_t*)&mAsyncHandle, [](uv_handle_t*) { /* do nothing */ });
        uv_stop(&mUvLoopHandle);
    });

    // Return the one true future that waits for the shutdown to complete.
    return mShutdownCompletePromise.get_future();
}

absl::Status LibuvEventLoopImpl::run() {
    mThreadId = std::this_thread::get_id();
    // Post a task to our own queue. When this task executes, we can be
    // certain that the event loop is actively processing events.
    (void)post([this]() { setState(LooperStatusEvent::State::RUNNING); });

    int err = uv_run(&mUvLoopHandle, UV_RUN_DEFAULT);

    setState(LooperStatusEvent::State::FINISHED);
    auto status = UvErrToAbslStatus(err);
    if (!mPromiseSet.exchange(true)) {
        mShutdownCompletePromise.set_value(status);
    }
    return status;
}

void LibuvEventLoopImpl::stop() {
    if (!mIsShuttingDown) {
        LOG(WARNING) << "The event loop is stopping without a call to shutdown! You will leak "
                        "handles.";
    }
    uv_stop(&mUvLoopHandle);
}

bool LibuvEventLoopImpl::isOnLoopThread() const {
    return std::this_thread::get_id() == mThreadId;
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

void LibuvEventLoopImpl::doPost(Task task) {
    {
        absl::MutexLock lock(&mTaskMutex);
        mTaskQueue.push(std::move(task));
    }
    uv_async_send(&mAsyncHandle);
}

void LibuvEventLoopImpl::postImpl(Task task, std::chrono::milliseconds delay) {
    if (mIsShuttingDown) {
        LOG(WARNING) << "LibuvEventLoopImpl is not available as it is shutting down";
        return;
    }

    if (delay == std::chrono::milliseconds::zero()) {
        doPost(std::move(task));
        return;
    }
    // For fire-and-forget, the timer's lifetime is managed by its own async
    // operations. We create it and immediately let go of the handle.
    auto timer = LibuvTimer::create(this, std::move(task), /*repeating=*/false);
    timer->start(delay.count(), 0);
}

std::shared_ptr<EventLoop::Timer> LibuvEventLoopImpl::scheduleDelayed(
        Task task, std::chrono::milliseconds delay) {
    auto timer = LibuvTimer::create(this, std::move(task), /*repeating=*/false);
    timer->start(delay.count(), 0);
    return std::make_shared<ScopedTimer>(timer);
}

std::shared_ptr<EventLoop::Timer> LibuvEventLoopImpl::scheduleRepeating(
        Task task, std::chrono::milliseconds initial_delay, std::chrono::milliseconds interval) {
    auto timer = LibuvTimer::create(this, std::move(task), /*repeating=*/true);
    timer->start(initial_delay.count(), interval.count());
    return std::make_shared<ScopedTimer>(timer);
}

}  // namespace goldfish::async
