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
#include <cstdint>
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

#include "android/base/threads/thread_utils.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/uv_to_absl.h"
#include "goldfish/perfetto/perfetto_categories.h"
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
 * Once the loop transitions to `kShuttingDown` or `kFinished`, it **will never**
 * revert to an active state. No new user tasks or timers will be accepted in these states.
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
    explicit LibuvEventLoopImpl(std::string name);
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
    void ShutdownTimers() override;
    size_t Flush() override;
    std::future<absl::Status> Shutdown() override;

    bool IsOnLoopThread() const override { return std::this_thread::get_id() == thread_id_; }

    void* GetRawLoop() override { return &uv_loop_handle_; }

    // --- Task Posting and Scheduling ---
    ScopedTimer CreateTimer(RepeatingTask task) override;

  private:
    friend class LibuvTimer;

    void AddActiveTimer(const std::shared_ptr<LibuvTimer>& t) {
        std::weak_ptr<LibuvTimer>& existing = active_timers_[t.get()];
        DCHECK(existing.expired()) << "Tried to insert a duplicate timer that was already active.";
        existing = t;
    }

    void RemoveActiveTimer(LibuvTimer* const t) {
        const size_t erased = active_timers_.erase(t);
        DCHECK(erased == 1)
                << "Tried to remove a timer that didn't exist in the active timers set.";
    }

    void ShutdownTimersInternal();

    struct QueuedTask {
        Task task;
        FlowId flow_id;
    };

    bool IsShuttingDownOrFinished() const {
        const LooperStatusEvent::State state = GetState();
        return state == LooperStatusEvent::State::kShuttingDown ||
               state == LooperStatusEvent::State::kFinished;
    }

    absl::Status PostDelayed(Task task, absl::Duration delay, FlowId flow_id) override;

    absl::Status PostImmediately(Task task, FlowId flow_id) override {
        if (IsShuttingDownOrFinished()) {
            LOG(WARNING) << "LibuvEventLoopImpl is not available as it is shutting down";
            return absl::UnavailableError("LibuvEventLoopImpl is shutting down");
        }
        PostImmediatelyInternal(std::move(task), flow_id);
        return absl::OkStatus();
    }

    void PostImmediatelyInternal(Task task, FlowId flow_id = 0);

    void ProcessTasks();

    uv_async_t* GetAsync() { return async_handle_valid_.load() ? &async_handle_ : nullptr; }

    uv_async_t* TakeOwnershipAsync() {
        return async_handle_valid_.exchange(false) ? &async_handle_ : nullptr;
    }

    /// The core libuv event loop instance.
    uv_loop_t uv_loop_handle_;

    /// A libuv async handle used to wake up the loop thread to process tasks.
    uv_async_t async_handle_;

    /// The thread ID of the thread currently running the event loop.
    std::atomic<std::thread::id> thread_id_;

    // A map of raw pointers to their corresponding weak pointers for safe shutdown.
    // Must be accessed only from the loop.
    absl::flat_hash_map<LibuvTimer*, std::weak_ptr<LibuvTimer>> active_timers_;

    /// Queue of tasks posted from external threads to be run on the loop.
    std::queue<QueuedTask> task_queue_ ABSL_GUARDED_BY(task_mutex_);
    uint64_t tasks_submitted_ ABSL_GUARDED_BY(task_mutex_){0};
    uint64_t tasks_processed_ ABSL_GUARDED_BY(task_mutex_){0};

    std::promise<absl::Status> shutdown_complete_promise_;

    // Keep all bools together to minimize alignment gaps.
    std::atomic<bool> async_handle_valid_{false};
    std::atomic<bool> promise_set_{false};

    // Mutexes placed at the very end.
    absl::Mutex task_mutex_;
    absl::Mutex lifecycle_mutex_;
};

// The timer now inherits from std::enable_shared_from_this to safely manage
// its lifecycle across the user handle and async callbacks.
//
// Handles should always be closed in the onclose callback
// We place shared_from_this in the .data handle in the event queue
class LibuvTimer : public EventLoop::Timer, public std::enable_shared_from_this<LibuvTimer> {
    struct Private {};

  public:
    enum class Ownership : uint8_t {
        kCallerScoped,  // Returned via ScopedTimer; disarms on false and stays reschedulable.
        kLoopManaged,   // Internal PostDelayed one-shot; self-cancels after firing.
    };

    // Factory function to ensure proper std::shared_ptr creation.
    // this sets up the mUvTimer with a self reference so it can be
    // placed in a queue.
    static std::shared_ptr<LibuvTimer> Create(LibuvEventLoopImpl* loop,
                                              EventLoop::RepeatingTask task, FlowId flow_id,
                                              Ownership ownership) {
        auto timer =
                std::make_shared<LibuvTimer>(loop, std::move(task), flow_id, ownership, Private());
        if (ownership == Ownership::kCallerScoped) {
            timer->caller_pin_ = timer;
        }
        {
            const absl::MutexLock lock(loop->lifecycle_mutex_);
            switch (loop->GetState()) {
            case LooperStatusEvent::State::kShuttingDown:
            case LooperStatusEvent::State::kFinished:
                // If the loop is shutting down or finished, disarm the timer so it cannot
                // execute or schedule work on a terminating loop.
                timer->cancelled_.store(true);
                return timer;

            case LooperStatusEvent::State::kNotStarted:
                // Run() has not started yet, so no loop thread exists (IsOnLoopThread() is
                // false) and lifecycle_mutex_ serializes uv_loop_handle_ and active_timers_.
                timer->InitOnLoop();
                return timer;

            case LooperStatusEvent::State::kRunning:
            case LooperStatusEvent::State::kPaused:
                break;
            }
        }
        if (loop->IsOnLoopThread()) {
            timer->InitOnLoop();
            return timer;
        }
        const absl::Status status = loop->PostAndWait([&timer]() {
            if (timer->event_loop_->IsShuttingDownOrFinished()) {
                timer->cancelled_.store(true);
                return;
            }
            timer->InitOnLoop();
        });
        if (!status.ok()) {
            timer->cancelled_.store(true);
        }
        return timer;
    }

    static void UnpinItselfOnClose(uv_handle_t* handle) {
        DCHECK(handle) << "UV handle cannot be null in close callback.";
        auto* uv_timer = reinterpret_cast<uv_timer_t*>(handle);
        DCHECK(uv_timer->data) << "UV timer handle must have a pointer to the LibuvTimer instance.";
        auto* that = static_cast<LibuvTimer*>(uv_timer->data);
        DCHECK(that->closing_);
        DCHECK(that->uv_pin_);

        that->uv_pin_.reset();  // potentially calls ~LibuvTimer
    }

    LibuvTimer(LibuvEventLoopImpl* loop, EventLoop::RepeatingTask task, FlowId flow_id,
               Ownership ownership, Private)
            : event_loop_(loop)
            , task_(std::move(task))
            , flow_id_(flow_id)
            , ownership_(ownership) {
        DCHECK(event_loop_);
    }

    void Cancel() override {
        auto caller_pin = std::move(caller_pin_);
        if (cancelled_.exchange(true)) {
            return;
        }
        if (event_loop_->IsOnLoopThread()) {
            DoCancel(false);
            return;
        }
        bool cancelled_while_not_running = false;
        {
            const absl::MutexLock lock(event_loop_->lifecycle_mutex_);
            const LooperStatusEvent::State state = event_loop_->GetState();
            if (state == LooperStatusEvent::State::kNotStarted ||
                state == LooperStatusEvent::State::kFinished) {
                DoCancel(false);
                cancelled_while_not_running = true;
            }
        }
        if (!cancelled_while_not_running) {
            event_loop_->PostAndWait([this]() { DoCancel(false); }).IgnoreError();
        }
    }

    void Schedule(absl::Duration new_delay, absl::Duration new_interval) override {
        // Because Timer::Cancel() is invoked solely by ~ScopedTimer(), a live
        // ScopedTimer is only marked cancelled_ when the loop is shutting down
        // or has finished, where EventLoop::Timer::Schedule() is a no-op.
        if (cancelled_.load()) {
            return;
        }
        const uint64_t seq = ++schedule_seq_;
        event_loop_->PostImmediatelyInternal([self = shared_from_this(), seq,
                                              new_delay_ms = absl::ToInt64Milliseconds(new_delay),
                                              new_interval_ms =
                                                      absl::ToInt64Milliseconds(new_interval)] {
            if (self->cancelled_.load() || !self->uv_pin_ || self->closing_) {
                return;
            }
            if (seq != self->schedule_seq_.load()) {
                return;
            }
            self->active_schedule_seq_ = seq;
            uv_timer_stop(&self->uv_timer_handle_);
            uv_timer_start(&self->uv_timer_handle_, OnTimerStatic, new_delay_ms, new_interval_ms);
        });
    }

  private:
    void InitOnLoop() {
        DCHECK(!uv_pin_) << "Timer should not be pinned before initialization.";
        if (const int err = uv_timer_init(&event_loop_->uv_loop_handle_, &uv_timer_handle_)) {
            LOG(DFATAL) << "uv_timer_init failed with: " << uv_strerror(err);
        }
        uv_timer_handle_.data = this;
        const auto self = shared_from_this();
        uv_pin_ = self;
        event_loop_->AddActiveTimer(self);
    }

    void DoCancel(const bool shutting_down) {
        cancelled_.store(true);
        if (uv_pin_ && !closing_) {
            closing_ = true;
            uv_timer_stop(&uv_timer_handle_);
            if (!shutting_down) {
                event_loop_->RemoveActiveTimer(this);
            }

            uv_close(reinterpret_cast<uv_handle_t*>(&uv_timer_handle_), UnpinItselfOnClose);
        }
    }

    static void OnTimerStatic(uv_timer_t* handle) {
        DCHECK(handle->data) << "UV timer handle must have a pointer to the LibuvTimer instance.";
        const auto self = static_cast<LibuvTimer*>(handle->data)->uv_pin_;
        DCHECK(self) << "OnTimerStatic callback called without a valid LibuvTimer instance.";
        DCHECK(self->event_loop_->IsOnLoopThread())
                << "OnTimerStatic callback must be executed on the event loop thread.";

        self->OnTimer();
    }

    void OnTimer() {
        if (cancelled_.load() || active_schedule_seq_ != schedule_seq_.load()) {
            return;
        }
        auto* tracker = event_loop_->tracker();
        if (flow_id_ != 0 && tracker) {
            tracker->LogExecute(flow_id_);
        }

        if (!task_()) {
            if (ownership_ == Ownership::kLoopManaged) {
                DoCancel(false);
            } else {
                uv_timer_stop(&uv_timer_handle_);
            }
        }
    }

    LibuvEventLoopImpl* const event_loop_;
    // Keeps LibuvTimer alive while the caller owns a ScopedTimer handle (until
    // TimerDeleter invokes Cancel()), even if the loop shuts down and closes
    // uv_timer_handle_ first.
    std::shared_ptr<LibuvTimer> caller_pin_;
    // Keeps LibuvTimer (which embeds uv_timer_handle_) alive from
    // uv_timer_init() until libuv invokes the asynchronous uv_close() callback
    // (UnpinItselfOnClose), even if the caller destroys ScopedTimer earlier.
    std::shared_ptr<LibuvTimer> uv_pin_;
    EventLoop::RepeatingTask task_;
    uv_timer_t uv_timer_handle_{};
    const FlowId flow_id_;

    // Monotonically increasing sequence number incremented on each Schedule() call.
    // Captured in the posted task to ensure that when multiple Schedule() calls
    // race or are posted in quick succession, older tasks running on the event loop
    // thread discard stale updates if superseded by a newer Schedule().
    std::atomic<uint64_t> schedule_seq_{0};

    // The sequence number of the schedule currently active on the loop backend.
    // Checked in OnTimer() to ensure an expiring timer does not fire if a newer
    // Schedule() has superseded it.
    uint64_t active_schedule_seq_ = 0;

    const Ownership ownership_;
    bool closing_ = false;
    std::atomic<bool> cancelled_{false};

    friend LibuvEventLoopImpl;
};

// --- LibuvEventLoopImpl Implementation ---

LibuvEventLoopImpl::LibuvEventLoopImpl(std::string name) : LibuvEventLoop(std::move(name)) {
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
    LOG_IF(FATAL, GetState() != LooperStatusEvent::State::kNotStarted &&
                          !IsShuttingDownOrFinished())
            << "Uv loop has not been shutdown prior to destruction";
    DCHECK(active_timers_.empty())
            << "All timers should have been cancelled and removed before the "
               "event loop is destroyed.";

    // Last ditch effort to close the wakeup handle if the loop was never shut down.
    if (uv_async_t* async = TakeOwnershipAsync()) {
        uv_close(reinterpret_cast<uv_handle_t*>(async), nullptr);
    }

    // Close any remaining handles and drain pending close callbacks so
    // uv_loop_close() does not fail with UV_EBUSY when handles (such as
    // subprocesses or stdio pipes) were unreferenced or closed after Run()
    // exited.
    constexpr int kMaxDrainIterations = 10;
    for (int i = 0; i < kMaxDrainIterations; ++i) {
        bool has_handles = false;
        uv_walk(
                &uv_loop_handle_,
                [](uv_handle_t* handle, void* arg) {
                    *static_cast<bool*>(arg) = true;
                    if (!uv_is_closing(handle)) {
                        uv_close(handle, nullptr);
                    }
                },
                &has_handles);
        if (!has_handles && uv_loop_alive(&uv_loop_handle_) == 0) {
            break;
        }
        uv_run(&uv_loop_handle_, UV_RUN_DEFAULT);
    }

    const int res = uv_loop_close(&uv_loop_handle_);
    if (res != 0) {
        LOG(WARNING) << "Failed to close uv_loop: " << uv_strerror(res);
        if (VLOG_IS_ON(1)) {
            LOG(WARNING) << "The following handles were leaked:";
            uv_print_all_handles(&uv_loop_handle_, stderr);
        }
    }
}

// Runs on the loop thread, or under lifecycle_mutex_ when the loop never started.
void LibuvEventLoopImpl::ShutdownTimersInternal() {
    for (const auto& [unsafe_ptr, weak_timer] : active_timers_) {
        if (const std::shared_ptr<LibuvTimer> timer = weak_timer.lock()) {
            timer->DoCancel(true);
        } else {
            // Note that we don't expect a timer to have been deleted without first calling
            // removeActiveTimer so "this should never happen"
        }
    }
    active_timers_.clear();
}

EventLoop::ScopedTimer LibuvEventLoopImpl::CreateTimer(RepeatingTask task) {
    auto timer = LibuvTimer::Create(this, std::move(task), /*flow_id=*/0,
                                    LibuvTimer::Ownership::kCallerScoped);
    return ScopedTimer(timer.get());
}

absl::Status LibuvEventLoopImpl::PostDelayed(Task task, absl::Duration delay, FlowId flow_id) {
    if (IsShuttingDownOrFinished()) {
        LOG(WARNING) << "LibuvEventLoopImpl is not available as it is shutting down";
        return absl::UnavailableError("LibuvEventLoopImpl is shutting down");
    }
    PostImmediatelyInternal([this, task = std::move(task), delay, flow_id]() mutable {
        if (IsShuttingDownOrFinished()) {
            return;
        }
        auto timer = LibuvTimer::Create(
                this,
                [task = std::move(task)]() mutable {
                    task();
                    return false;
                },
                flow_id, LibuvTimer::Ownership::kLoopManaged);
        timer->Schedule(delay, absl::ZeroDuration());
    });
    return absl::OkStatus();
}

void LibuvEventLoopImpl::PostImmediatelyInternal(Task task, FlowId flow_id) {
    // We allow tasks to be queued before the loop has been started.
    {
        const absl::MutexLock lock(task_mutex_);
        ++tasks_submitted_;
        task_queue_.push(QueuedTask{std::move(task), flow_id});
    }
    if (uv_async_t* uv_async = GetAsync()) {
        uv_async_send(uv_async);
    }
}

void LibuvEventLoopImpl::ProcessTasks() {
    std::queue<QueuedTask> tasks;
    {
        const absl::MutexLock lock(task_mutex_);
        tasks.swap(task_queue_);
    }

    const size_t num_tasks_to_process = tasks.size();
    TRACE_EVENT("async", "LibuvEventLoop::ProcessTasks", "batch_size", num_tasks_to_process);

    while (!tasks.empty()) {
        FlowId flow_id = tasks.front().flow_id;
        if (flow_id != 0 && tracker()) {
            tracker()->LogExecute(flow_id);
        }
        tasks.front().task();
        tasks.pop();
    }

    const absl::MutexLock lock(task_mutex_);
    tasks_processed_ += num_tasks_to_process;
}

absl::Status LibuvEventLoopImpl::Run() {
    bool notify_running = false;
    {
        const absl::MutexLock lock(lifecycle_mutex_);
        if (GetState() != LooperStatusEvent::State::kNotStarted) {
            // Shutdown() may complete before the thread spawned to call Run() gets here.
            // Return an error so that thread exits instead of reviving a finished loop.
            return absl::FailedPreconditionError(
                    "Cannot run event loop '" + GetName() +
                    "': it has already been shut down or has finished running. Create a new loop.");
        }
        thread_id_ = std::this_thread::get_id();
        notify_running = SetStateQuietly(LooperStatusEvent::State::kRunning);
    }
    android::base::ThreadUtils::SetCurrentThreadName(GetName());
    if (notify_running) {
        FireEvent({.state = LooperStatusEvent::State::kRunning});
    }

    const int err = uv_run(&uv_loop_handle_, UV_RUN_DEFAULT);
    bool notify_finished = false;
    {
        const absl::MutexLock lock(lifecycle_mutex_);
        notify_finished = SetStateQuietly(LooperStatusEvent::State::kFinished);
    }
    if (notify_finished) {
        FireEvent({.state = LooperStatusEvent::State::kFinished});
    }

    auto status = UvErrToAbslStatus(err);
    if (!promise_set_.exchange(true)) {
        shutdown_complete_promise_.set_value(status);
    }
    return status;
}

void LibuvEventLoopImpl::ShutdownTimers() {
    PostAndWait([this]() { ShutdownTimersInternal(); }).IgnoreError();
}

size_t LibuvEventLoopImpl::Flush() {
    const absl::MutexLock lock(task_mutex_);
    const uint64_t target = tasks_submitted_;
    auto reached_target = [this, target]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(task_mutex_) {
        return HasProcessedTarget(tasks_processed_, target);
    };
    task_mutex_.Await(absl::Condition(&reached_target));
    return static_cast<size_t>(tasks_processed_);
}

std::future<absl::Status> LibuvEventLoopImpl::Shutdown() {
    std::promise<absl::Status> promise;
    LooperStatusEvent::State next_state = LooperStatusEvent::State::kShuttingDown;
    bool state_changed = false;
    {
        const absl::MutexLock lock(lifecycle_mutex_);
        const LooperStatusEvent::State state = GetState();
        if (state == LooperStatusEvent::State::kNotStarted) {
            // No thread runs the loop, so lifecycle_mutex_ serializes cancelling any
            // pre-Run() timers; ~LibuvEventLoopImpl() closes async_handle_.
            next_state = LooperStatusEvent::State::kFinished;
            state_changed = SetStateQuietly(next_state);
            ShutdownTimersInternal();
        } else {
            if (state != LooperStatusEvent::State::kRunning) {
                promise.set_value(absl::InvalidArgumentError(
                        "You cannot shutdown a loop that is not running."));
                return promise.get_future();
            }
            if (IsOnLoopThread()) {
                LOG(FATAL) << "Shutdown() must not be called on LibuvEventLoop '" << GetName()
                           << "' from its own loop thread; call Shutdown() from another thread.";
            }
            state_changed = SetStateQuietly(next_state);
        }
    }

    if (state_changed) {
        FireEvent({.state = next_state});
    }
    if (next_state == LooperStatusEvent::State::kFinished) {
        promise.set_value(absl::OkStatus());
        return promise.get_future();
    }

    // Post the actual shutdown logic using the private postImmediately.
    PostImmediatelyInternal([this]() {
        ShutdownTimersInternal();

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

std::unique_ptr<LibuvEventLoop> LibuvEventLoop::Create(std::string name) {
    return std::make_unique<LibuvEventLoopImpl>(std::move(name));
}

}  // namespace goldfish::async
