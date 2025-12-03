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
#include <mutex>
#include <queue>
#include <thread>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/synchronization/notification.h"

#include "goldfish/QEMUBH.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/scoped_async_timer.h"

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
using goldfish::qemu::make_qemu_bh;
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
        static std::shared_ptr<QemuTimer> create(QemuEventLoopImpl* loop, Task task,
                                                 bool auto_cancel) {
            auto timer = std::make_shared<QemuTimer>(loop, std::move(task), auto_cancel, Private());
            timer->addItselfToActiveTimers();
            return timer;
        }

        QemuTimer(QemuEventLoopImpl* loop, EventLoop::Task task, bool auto_cancel, Private)
                : mEventLoop(loop), mTask(std::move(task)), mAutoCancel(auto_cancel) {}

        ~QemuTimer() override {}

        void doCancel() {
            if (QEMUTimer* qemuTimer = takeOwnershipQemuTimer()) {
                // This should stop and un-register the timer.
                timer_del(qemuTimer);
                DCHECK(!mQemuTimerHandleValid.load());
                mEventLoop.load()->removeActiveTimer(this);
                mEventLoop.store(nullptr);
                mPinned.reset();  // potentially calls dtor
            }
        }

        void cancel() override {
            if (auto* loop = mEventLoop.load()) {
                // Stop and delete the timer from the event loop.
                loop->postImmediatelyInternal([self = shared_from_this()]() { self->doCancel(); });
            } else {
                LOG(ERROR) << "Can't cancel a timer after it has been cancelled";
            }
        }

        void schedule(std::chrono::milliseconds new_delay,
                      std::chrono::milliseconds new_interval) override {
            if (auto* loop = mEventLoop.load()) {
                loop->postImmediatelyInternal([self = shared_from_this(),
                                               new_delay_ms = new_delay.count(),
                                               new_interval_ms = new_interval.count()] {
                    self->mInterval_ms = new_interval_ms;
                    if (QEMUTimer* qemuTimer = self->getQemuTimer()) {
                        timer_mod(qemuTimer, qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + new_delay_ms);
                    }
                });
            } else {
                LOG(ERROR) << "Can't reschedule a timer after it has been cancelled";
            }
        }

      private:
        void addItselfToActiveTimers() {
            // shared_from_this() is not available in the ctor
            auto* loop = mEventLoop.load();
            loop->postImmediatelyInternal([loop, self = shared_from_this()]() {
                DCHECK(!self->mPinned);
                self->mPinned = self;
                timer_init_ms(&self->mQemuTimerHandle, QEMU_CLOCK_REALTIME, &QemuTimer::onTimer,
                              self.get());
                DCHECK(!self->mQemuTimerHandleValid.load());
                self->mQemuTimerHandleValid.store(true);

                loop->addActiveTimer(self);
            });
        }

        // C-style callback passed to QEMU.
        static void onTimer(void* opaque) {
            const auto self = static_cast<QemuTimer*>(opaque)->mPinned;
            DCHECK(self) << "onTimer callback is called without a shared_from_this pointer";
            DCHECK(self->mEventLoop.load()->isOnLoopThread())
                    << "onTimer callback is not called from the event loop";

            self->mTask();

            // For one-shot timers, close the handle after execution.
            // This will lead to the object being deleted if the user has
            // also released their shared_ptr.
            if (self->mAutoCancel) {
                self->doCancel();
            } else if (self->mInterval_ms != 0) {
                // The Qemu Timer API doesn't natively support repeating timers so we have to kick
                // it off again.
                if (QEMUTimer* qemuTimer = self->getQemuTimer()) {
                    timer_mod(qemuTimer,
                              qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + self->mInterval_ms);
                }
            }
        }

        QEMUTimer* getQemuTimer() {
            return mQemuTimerHandleValid.load() ? &mQemuTimerHandle : nullptr;
        }

        QEMUTimer* takeOwnershipQemuTimer() {
            return mQemuTimerHandleValid.exchange(false) ? &mQemuTimerHandle : nullptr;
        }

        std::atomic<QemuEventLoopImpl*> mEventLoop;
        Task mTask;
        bool mAutoCancel = false;

        QEMUTimer mQemuTimerHandle;
        std::atomic<bool> mQemuTimerHandleValid = false;

        std::shared_ptr<QemuTimer> mPinned;  ///< prevents calling the dctor
        uint64_t mInterval_ms = 0;
    };

  public:
    QemuEventLoopImpl() : mDrainerBh(make_qemu_bh([&] { drainQueue(); })) {
        setState(LooperStatusEvent::State::RUNNING);
    }

    // TODO(whollins): Clean-up usages and make this FATAL.
    ~QemuEventLoopImpl() override {
        LOG_IF(ERROR, !mIsShuttingDown) << "Qemu loop has not been shutdown prior to destruction";
        DCHECK(mActiveTimers.empty());
    };

    std::future<absl::Status> shutdown() override;

    bool isOnLoopThread() const override;

    std::shared_ptr<EventLoop::Timer> createTimer(Task task) override;

  private:
    void postImmediatelyInternal(Task task);
    absl::Status postImmediately(Task task) override;
    absl::Status postDelayed(Task task, std::chrono::milliseconds delay) override;

    void drainQueue() {
        mQemuThreadId = std::this_thread::get_id();
        std::queue<Task> local_queue;
        {
            std::lock_guard<std::mutex> lock(mQueueMutex);
            mTaskQueue.swap(local_queue);
            mDrainerScheduled = false;
        }

        while (!local_queue.empty()) {
            local_queue.front()();
            local_queue.pop();
        }
    }

    void addActiveTimer(const std::shared_ptr<QemuTimer>& t) {
        LOG_IF(DFATAL, !isOnLoopThread()) << "addActiveTimer must be called from the loop thread";
        std::weak_ptr<QemuTimer>& existing = mActiveTimers[t.get()];
        DCHECK(existing.expired()) << "Tried to insert a duplicate timer";
        existing = t;
    }

    void removeActiveTimer(QemuTimer* const t) {
        LOG_IF(DFATAL, !isOnLoopThread())
                << "removeActiveTimer must be called from the loop thread";
        const size_t erased = mActiveTimers.erase(t);
        DCHECK(erased == 1) << "Tried to remove a timer that didn't exist";
    }

    void shutdownTimers() {
        LOG_IF(DFATAL, !isOnLoopThread()) << "shutdownTimers must be called from the loop thread";
        // Iterate a copy as doCancel calls back to removeActiveTimer which calls erase.
        auto copy = mActiveTimers;
        for (const auto& [unsafePtr, weakTimer] : copy) {
            if (std::shared_ptr<QemuTimer> timer = weakTimer.lock()) {
                timer->doCancel();
            } else {
                // Note that we don't expect a timer to have been deleted without first calling
                // removeActiveTimer so "this should never happen"
            }
        }
    }

    std::atomic<bool> mIsShuttingDown{false};
    std::promise<absl::Status> mShutdownCompletePromise;

    std::thread::id mQemuThreadId;
    std::mutex mQueueMutex;
    std::queue<Task> mTaskQueue;
    QEMUBHPtr mDrainerBh;
    bool mDrainerScheduled = false;

    // A map of raw pointers to their corresponding weak pointers for safe shutdown.
    // Must only be accessed from the Qemu thread.
    absl::flat_hash_map<QemuTimer*, std::weak_ptr<QemuTimer>> mActiveTimers;
};

// --- QemuEventLoopImpl Method Implementations ---

std::future<absl::Status> QemuEventLoopImpl::shutdown() {
    if (mIsShuttingDown.exchange(true)) {
        std::promise<absl::Status> promise;
        promise.set_value(absl::InvalidArgumentError("This loop has already been shutdown"));
        return promise.get_future();
    }
    setState(LooperStatusEvent::State::SHUTTING_DOWN);

    auto do_shutdown = [this] {
        shutdownTimers();
        mShutdownCompletePromise.set_value(absl::OkStatus());
    };
    if (isOnLoopThread()) {
        do_shutdown();
    } else {
        postImmediatelyInternal(do_shutdown);
    }

    return mShutdownCompletePromise.get_future();
}

bool QemuEventLoopImpl::isOnLoopThread() const {
    return mQemuThreadId == std::this_thread::get_id();
}

void QemuEventLoopImpl::postImmediatelyInternal(Task task) {
    std::lock_guard<std::mutex> lock(mQueueMutex);
    mTaskQueue.push(std::move(task));

    if (!mDrainerScheduled) {
        qemu_bh_schedule(mDrainerBh.get());
        mDrainerScheduled = true;
    }
}

absl::Status QemuEventLoopImpl::postImmediately(Task task) {
    if (mIsShuttingDown) {
        LOG(ERROR) << "Event loop is shutting down, not scheduling task";
        return absl::UnavailableError("QemuEventLoopImpl is shutting down");
    }

    postImmediatelyInternal(std::move(task));
    return absl::OkStatus();
}

absl::Status QemuEventLoopImpl::postDelayed(Task task, std::chrono::milliseconds delay) {
    if (mIsShuttingDown) {
        LOG(ERROR) << "Event loop is shutting down, not scheduling task";
        return absl::UnavailableError("QemuEventLoopImpl is shutting down");
    }

    // The timer will manage its own lifetime via a shared_ptr cycle that is
    // broken when the timer fires.
    auto timer = QemuTimer::create(this, std::move(task), /*auto_cancel=*/true);
    timer->schedule(delay, std::chrono::milliseconds::zero());
    return absl::OkStatus();
}

std::shared_ptr<EventLoop::Timer> QemuEventLoopImpl::createTimer(Task task) {
    if (mIsShuttingDown) {
        return std::make_shared<ScopedTimer>(nullptr);
    }
    auto timer = QemuTimer::create(this, std::move(task), /*auto_cancel=*/false);
    return std::make_shared<ScopedTimer>(timer);
}

}  // namespace

// --- Factory Function ---
std::unique_ptr<QemuEventLoop> QemuEventLoop::create() {
    auto loop = std::make_unique<QemuEventLoopImpl>();
    (void)loop->post(
            [loop_ptr = loop.get()] { loop_ptr->setState(LooperStatusEvent::State::RUNNING); });

    return loop;
}

}  // namespace goldfish::async
