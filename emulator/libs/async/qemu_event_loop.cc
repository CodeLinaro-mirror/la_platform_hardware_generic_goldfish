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

// This file provides an implementation of the EventLoop interface based on the
// main event loop provided by the QEMU emulator. It allows scheduling tasks
// (timers, immediate callbacks) on the main QEMU thread from anywhere in the
// application.
//
// The QemuEventLoopImpl is a singleton, accessible via `getQemuEventLoop()`.
// It's crucial to call `initializeQemuEventLoop()` from the main QEMU thread
// at startup before any other thread attempts to use the loop. This sets a
// thread-local flag that allows `isOnLoopThread()` to work correctly.

#include "goldfish/async/qemu_event_loop.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <queue>
#include <thread>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/async/scoped_async_timer.h"
#include "goldfish/qemu/qemubh.h"

extern "C" {

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"


// Windows workarounds
#ifdef shutdown
#undef shutdown
#endif
#ifdef close
#undef close
#endif
// IWYU pragma: end_keep
// clang-format on
}

namespace goldfish::async {
namespace {

using goldfish::async::LooperStatusEvent;
using goldfish::async::ScopedTimer;
using goldfish::qemu::MakeQemuBh;
using goldfish::qemu::QEMUBHPtr;

// An implementation of the EventLoop interface that is backed by the QEMU main
// event loop. This allows scheduling work on the main QEMU thread.
class QemuEventLoopImpl : public goldfish::async::QemuEventLoop {
  public:
    // --- QemuTimer Implementation ---
    // An implementation of the Timer interface that wraps a QEMUTimer.
    //
    // This class is responsible for managing the lifetime of cancellable delayed
    // and repeating tasks scheduled on the QemuEventLoopImpl.
    class QemuTimer : public EventLoop::Timer, public std::enable_shared_from_this<QemuTimer> {
        struct Private {};

      public:
        static std::shared_ptr<QemuTimer> Create(QemuEventLoopImpl* loop, Task task,
                                                 bool auto_cancel, FlowId flow_id = 0) {
            auto timer = std::make_shared<QemuTimer>(loop, std::move(task), auto_cancel, flow_id,
                                                     Private());
            timer->AddItselfToActiveTimers();
            return timer;
        }

        QemuTimer(QemuEventLoopImpl* loop, EventLoop::Task task, bool auto_cancel, FlowId flow_id,
                  Private)
                : event_loop_(loop)
                , task_(std::move(task))
                , flow_id_(flow_id)
                , auto_cancel_(auto_cancel) {}

        ~QemuTimer() override = default;

        void DoCancel() {
            if (QEMUTimer* qemu_timer = TakeOwnershipQemuTimer()) {
                // This should stop and un-register the timer.
                timer_del(qemu_timer);
                DCHECK(!qemu_timer_handle_valid_.load());
                event_loop_.load()->RemoveActiveTimer(this);
                event_loop_.store(nullptr);
                pinned_.reset();  // potentially calls dtor
            }
        }

        void Cancel() override {
            if (auto* loop = event_loop_.load()) {
                // Stop and delete the timer from the event loop.
                loop->PostImmediatelyInternal([self = shared_from_this()]() { self->DoCancel(); });
            } else {
                LOG(ERROR) << "Can't cancel a timer after it has been cancelled";
            }
        }

        void Schedule(std::chrono::milliseconds new_delay,
                      std::chrono::milliseconds new_interval) override {
            if (auto* loop = event_loop_.load()) {
                loop->PostImmediatelyInternal([self = shared_from_this(),
                                               new_delay_ms = new_delay.count(),
                                               new_interval_ms = new_interval.count()] {
                    self->interval_ms_ = new_interval_ms;
                    if (QEMUTimer* qemu_timer = self->GetQemuTimer()) {
                        timer_mod(qemu_timer,
                                  qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + new_delay_ms);
                    }
                });
            } else {
                LOG(ERROR) << "Can't reschedule a timer after it has been cancelled";
            }
        }

      private:
        void AddItselfToActiveTimers() {
            // shared_from_this() is not available in the ctor
            auto* loop = event_loop_.load();
            loop->PostImmediatelyInternal([loop, self = shared_from_this()]() {
                DCHECK(!self->pinned_);
                self->pinned_ = self;
                timer_init_ms(&self->qemu_timer_handle_, QEMU_CLOCK_REALTIME, &QemuTimer::OnTimer,
                              self.get());
                DCHECK(!self->qemu_timer_handle_valid_.load());
                self->qemu_timer_handle_valid_.store(true);

                loop->AddActiveTimer(self);
            });
        }

        // C-style callback passed to QEMU.
        static void OnTimer(void* opaque) {
            const auto self = static_cast<QemuTimer*>(opaque)->pinned_;
            DCHECK(self) << "onTimer callback is called without a shared_from_this pointer";
            DCHECK(self->event_loop_.load()->IsOnLoopThread())
                    << "onTimer callback is not called from the event loop";

            if (self->flow_id_ != 0 && self->event_loop_.load()->tracker()) {
                self->event_loop_.load()->tracker()->LogExecute(self->flow_id_);
            }

            self->task_();

            // For one-shot timers, close the handle after execution.
            // This will lead to the object being deleted if the user has
            // also released their shared_ptr.
            if (self->auto_cancel_) {
                self->DoCancel();
            } else if (self->interval_ms_ != 0) {
                // The Qemu Timer API doesn't natively support repeating timers so we have to kick
                // it off again.
                if (QEMUTimer* qemu_timer = self->GetQemuTimer()) {
                    timer_mod(qemu_timer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) +
                                                  static_cast<int64_t>(self->interval_ms_));
                }
            }
        }

        QEMUTimer* GetQemuTimer() {
            return qemu_timer_handle_valid_.load() ? &qemu_timer_handle_ : nullptr;
        }

        QEMUTimer* TakeOwnershipQemuTimer() {
            return qemu_timer_handle_valid_.exchange(false) ? &qemu_timer_handle_ : nullptr;
        }

        std::atomic<QemuEventLoopImpl*> event_loop_;
        std::shared_ptr<QemuTimer> pinned_;  ///< prevents calling the dctor
        Task task_;
        QEMUTimer qemu_timer_handle_;
        const FlowId flow_id_;
        uint64_t interval_ms_ = 0;
        const bool auto_cancel_;
        std::atomic<bool> qemu_timer_handle_valid_ = false;
    };

    QemuEventLoopImpl(std::string name)
            : QemuEventLoop(std::move(name)), drainer_bh_(MakeQemuBh([&] { DrainQueue(); })) {
        SetState(LooperStatusEvent::State::kRunning);
    }

    // TODO(whollins): Clean-up usages and make this FATAL.
    ~QemuEventLoopImpl() override {
        LOG_IF(ERROR, !is_shutting_down_) << "Qemu loop has not been shutdown prior to destruction";
        DCHECK(active_timers_.empty());
    };

    void ShutdownTimers() override;
    size_t WaitUntilIdle() override;
    std::future<absl::Status> Shutdown() override;

    bool IsOnLoopThread() const override;

    std::shared_ptr<EventLoop::Timer> CreateTimer(Task task) override;

  private:
    struct QueuedTask {
        Task task;
        FlowId flow_id;
    };

    void PostImmediatelyInternal(Task task, FlowId flow_id = 0);
    absl::Status PostImmediately(Task task, FlowId flow_id) override;
    absl::Status PostDelayed(Task task, std::chrono::milliseconds delay, FlowId flow_id) override;

    size_t DrainQueue() {
        qemu_thread_id_ = std::this_thread::get_id();
        std::queue<QueuedTask> local_queue;
        {
            const absl::MutexLock lock(queue_mutex_);
            task_queue_.swap(local_queue);
            drainer_scheduled_ = false;
        }

        const size_t num_tasks_to_process = local_queue.size();

        while (!local_queue.empty()) {
            FlowId flow_id = local_queue.front().flow_id;
            if (flow_id != 0 && tracker()) {
                tracker()->LogExecute(flow_id);
            }
            local_queue.front().task();
            local_queue.pop();
        }

        absl::MutexLock lock(queue_mutex_);
        queue_is_idle_ = task_queue_.empty();
        tasks_processed_ += num_tasks_to_process;
        return tasks_processed_;
    }

    void AddActiveTimer(const std::shared_ptr<QemuTimer>& t) {
        LOG_IF(DFATAL, !IsOnLoopThread()) << "addActiveTimer must be called from the loop thread";
        std::weak_ptr<QemuTimer>& existing = active_timers_[t.get()];
        DCHECK(existing.expired()) << "Tried to insert a duplicate timer";
        existing = t;
    }

    void RemoveActiveTimer(QemuTimer* const t) {
        LOG_IF(DFATAL, !IsOnLoopThread())
                << "removeActiveTimer must be called from the loop thread";
        const size_t erased = active_timers_.erase(t);
        DCHECK(erased == 1) << "Tried to remove a timer that didn't exist";
    }

    void ShutdownTimersInternal() {
        LOG_IF(DFATAL, !IsOnLoopThread())
                << "ShutdownTimersInternal must be called from the loop thread";
        // Iterate a copy as doCancel calls back to removeActiveTimer which calls erase.
        auto copy = active_timers_;
        for (const auto& [unsafePtr, weakTimer] : copy) {
            if (const std::shared_ptr<QemuTimer> timer = weakTimer.lock()) {
                timer->DoCancel();
            } else {
                // Note that we don't expect a timer to have been deleted without first calling
                // removeActiveTimer so "this should never happen"
            }
        }
    }

    const QEMUBHPtr drainer_bh_;
    std::promise<absl::Status> shutdown_complete_promise_;

    std::atomic<std::thread::id> qemu_thread_id_;
    absl::Mutex queue_mutex_;
    std::queue<QueuedTask> task_queue_ ABSL_GUARDED_BY(queue_mutex_);
    size_t tasks_processed_ ABSL_GUARDED_BY(queue_mutex_) = 0;
    std::atomic<bool> is_shutting_down_{false};
    bool drainer_scheduled_ ABSL_GUARDED_BY(queue_mutex_) = false;
    bool queue_is_idle_ ABSL_GUARDED_BY(queue_mutex_) = true;

    // A map of raw pointers to their corresponding weak pointers for safe shutdown.
    // Must only be accessed from the Qemu thread.
    absl::flat_hash_map<QemuTimer*, std::weak_ptr<QemuTimer>> active_timers_;
};

// --- QemuEventLoopImpl Method Implementations ---

void QemuEventLoopImpl::ShutdownTimers() {
    PostImmediatelyInternal([this]() { ShutdownTimersInternal(); });
}

size_t QemuEventLoopImpl::WaitUntilIdle() {
    if (IsOnLoopThread()) {
        return DrainQueue();
    } else {
        const absl::MutexLock lock(queue_mutex_);
        if (!queue_mutex_.AwaitWithTimeout(absl::Condition(&queue_is_idle_), absl::Seconds(15))) {
            LOG(FATAL) << "Timed out waiting for queue to be idle";
        }
        return tasks_processed_;
    }
}

std::future<absl::Status> QemuEventLoopImpl::Shutdown() {
    if (is_shutting_down_.exchange(true)) {
        std::promise<absl::Status> promise;
        promise.set_value(absl::InvalidArgumentError("This loop has already been shutdown"));
        return promise.get_future();
    }
    SetState(LooperStatusEvent::State::kShuttingDown);

    auto do_shutdown = [this] {
        ShutdownTimersInternal();
        shutdown_complete_promise_.set_value(absl::OkStatus());
    };
    if (IsOnLoopThread()) {
        do_shutdown();
    } else {
        PostImmediatelyInternal(do_shutdown);
    }

    return shutdown_complete_promise_.get_future();
}

bool QemuEventLoopImpl::IsOnLoopThread() const {
    return qemu_thread_id_ == std::this_thread::get_id();
}

void QemuEventLoopImpl::PostImmediatelyInternal(Task task, FlowId flow_id) {
    const absl::MutexLock lock(queue_mutex_);
    queue_is_idle_ = false;
    task_queue_.push(QueuedTask{std::move(task), flow_id});

    if (!drainer_scheduled_) {
        qemu_bh_schedule(drainer_bh_.get());
        drainer_scheduled_ = true;
    }
}

absl::Status QemuEventLoopImpl::PostImmediately(Task task, FlowId flow_id) {
    if (is_shutting_down_) {
        LOG(ERROR) << "Event loop is shutting down, not scheduling task";
        return absl::UnavailableError("QemuEventLoopImpl is shutting down");
    }

    PostImmediatelyInternal(std::move(task), flow_id);
    return absl::OkStatus();
}

absl::Status QemuEventLoopImpl::PostDelayed(Task task, std::chrono::milliseconds delay,
                                            FlowId flow_id) {
    if (is_shutting_down_) {
        LOG(ERROR) << "Event loop is shutting down, not scheduling task";
        return absl::UnavailableError("QemuEventLoopImpl is shutting down");
    }

    // The timer will manage its own lifetime via a shared_ptr cycle that is
    // broken when the timer fires.
    auto timer = QemuTimer::Create(this, std::move(task), /*auto_cancel=*/true, flow_id);
    timer->Schedule(delay, std::chrono::milliseconds::zero());
    return absl::OkStatus();
}

std::shared_ptr<EventLoop::Timer> QemuEventLoopImpl::CreateTimer(Task task) {
    if (is_shutting_down_) {
        return std::make_shared<ScopedTimer>(nullptr);
    }
    auto timer = QemuTimer::Create(this, std::move(task), /*auto_cancel=*/false);
    return std::make_shared<ScopedTimer>(timer);
}

}  // namespace

// --- Factory Function ---
std::unique_ptr<QemuEventLoop> QemuEventLoop::Create(std::string name) {
    auto loop = std::make_unique<QemuEventLoopImpl>(std::move(name));
    loop->Post([loop_ptr = loop.get()] {
            loop_ptr->SetState(LooperStatusEvent::State::kRunning);
        }).IgnoreError();

    return loop;
}

}  // namespace goldfish::async
