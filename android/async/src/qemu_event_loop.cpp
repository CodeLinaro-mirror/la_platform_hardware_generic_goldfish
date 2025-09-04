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

#include "absl/base/call_once.h"
#include "absl/log/log.h"

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

namespace {

using goldfish::async::EventLoop;
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
      public:
        QemuTimer(Task task, std::chrono::milliseconds delay, std::chrono::milliseconds interval,
                  QemuEventLoopImpl* loop)
                : mTask(std::move(task)), mDelay(delay), mInterval(interval), mLoop(loop) {}

        ~QemuTimer() override { VLOG(1) << "QemuTimer is out of scope"; }

        // Schedules the QEMU timer.
        void start() {
            if (mCancelled) return;

            // Lifetime Management with `mSelf`:
            // The QEMU timer API is a C-style API that uses a raw `void*` opaque
            // pointer. It has no knowledge of C++ smart pointers like `std::shared_ptr`.
            //
            // We need to ensure that the `QemuTimer` object remains alive until its
            // corresponding QEMU timer either fires or is cancelled. If the user
            // drops their `shared_ptr<Timer>` handle, the `QemuTimer` could be
            // destroyed prematurely, leaving the QEMU timer with a dangling
            // `opaque` pointer.
            //
            // To solve this, we create a self-referencing `shared_ptr`. The `mSelf`
            // member holds a `shared_ptr` to `this` object. This increases the
            // `shared_ptr` reference count by one, preventing the object from being
            // destroyed even if the user's handle goes out of scope.
            //
            // This self-reference is a deliberate, temporary cycle that we must
            // manually break when the timer's work is done (either by firing or
            // by being cancelled). See `handleFire()` and `cancel()` for where
            // `mSelf.reset()` is called to break the cycle.
            mSelf = shared_from_this();
            mQemuTimer = timer_new_ms(QEMU_CLOCK_HOST, &QemuTimer::qemuCallback, this);
            timer_mod(mQemuTimer, qemu_clock_get_ms(QEMU_CLOCK_HOST) + mDelay.count());
        }

        void cleanup() {
            // The timer is still pending. We need to safely free the underlying
            // QEMUTimer from the QEMU main thread. We post a task to do this.
            // The lambda captures `mSelf` to ensure `this` is still alive when
            // the cleanup task runs.
            if (mCleanup.exchange(true)) {
                return;
            };

            // Guaranteed to run once b/441087461 is fixed.
            mLoop->post([qemu_timer = mQemuTimer, self = mSelf]() {
                // This will both remove the timers that are scheduled
                // and cleanup any resources. Since we are on the loop thread ourselves
                // we can guarantee that no timer is active.
                timer_free(qemu_timer);

                // Break the self-reference cycle to allow destruction if needed.
                self->mSelf.reset();
            });
        }

        // Cancels the timer. This is thread-safe.
        void cancel() override {
            if (mCancelled.exchange(true)) {
                return;
            }
            cleanup();
        }

      private:
        // C-style callback passed to QEMU.
        static void qemuCallback(void* opaque) {
            auto self = static_cast<QemuTimer*>(opaque);
            self->handleFire();
        }

        // Called when the QEMU timer fires.
        void handleFire() {
            // Note: Only on thread is every active here.
            if (mCancelled.load()) {
                // Can happen if cancel() is called just as the timer fires.
                cleanup();
                return;
            }

            mTask();

            if (mInterval.count() > 0 && !mCancelled.load()) {
                timer_mod(mQemuTimer, qemu_clock_get_ms(QEMU_CLOCK_HOST) + mInterval.count());
            } else {
                // It's a one-shot timer or we are cancelled. The work is done.
                cleanup();
            }
        }

        QEMUTimer* mQemuTimer = nullptr;
        Task mTask;
        std::chrono::milliseconds mDelay;
        std::chrono::milliseconds mInterval;
        QemuEventLoopImpl* mLoop;
        std::atomic<bool> mCancelled{false};
        std::atomic<bool> mCleanup{false};  // Only one cleanup ever.
        std::shared_ptr<QemuTimer> mSelf;   // Manages the object's lifetime.
    };

  public:
    QemuEventLoopImpl() : mDrainerBh(make_qemu_bh([&] { drainQueue(); })) {
        setState(LooperStatusEvent::State::RUNNING);
    }

    // TODO(jansene): b/441087461 make sure deletion happens on qemu thread.
    ~QemuEventLoopImpl() override { drainQueue(); };

    absl::Status run() override;
    void stop() override;
    std::future<absl::Status> shutdown(std::chrono::milliseconds timeout) override;
    bool isOnLoopThread() const override;

    void postImpl(Task task, std::chrono::milliseconds delay) override;
    std::shared_ptr<Timer> scheduleDelayed(Task task, std::chrono::milliseconds delay) override;
    std::shared_ptr<Timer> scheduleRepeating(Task task, std::chrono::milliseconds initial_delay,
                                             std::chrono::milliseconds interval) override;
    void* getRawLoop() const override;

  private:
    void postImmediately(Task task);

    void drainQueue() {
        mQemuThreadId = std::this_thread::get_id();
        LOG(INFO) << "Currently on: " << std::this_thread::get_id();
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

    std::atomic<bool> mIsShuttingDown{false};

    std::thread::id mQemuThreadId;
    std::mutex mQueueMutex;
    std::queue<Task> mTaskQueue;
    QEMUBHPtr mDrainerBh;
    bool mDrainerScheduled = false;
};

// --- QemuEventLoopImpl Method Implementations ---

absl::Status QemuEventLoopImpl::run() {
    LOG(WARNING) << "QemuEventLoopImpl::run() should not be called. The QEMU main "
                    "loop is managed by the application.";
    return absl::UnimplementedError("run() is not supported");
}

void QemuEventLoopImpl::stop() {
    LOG(WARNING) << "QemuEventLoopImpl::stop() should not be called. The QEMU main "
                    "loop is managed by the application.";
}

std::future<absl::Status> QemuEventLoopImpl::shutdown(std::chrono::milliseconds timeout) {
    setState(LooperStatusEvent::State::SHUTTING_DOWN);
    mIsShuttingDown.store(true);
    std::promise<absl::Status> promise;
    promise.set_value(absl::OkStatus());
    return promise.get_future();
}

bool QemuEventLoopImpl::isOnLoopThread() const {
    return mQemuThreadId == std::this_thread::get_id();
}

void QemuEventLoopImpl::postImmediately(Task task) {
    if (mIsShuttingDown) {
        LOG(ERROR) << "Event loop is shutting down, task is not scheduled.";
        return;
    }

    std::lock_guard<std::mutex> lock(mQueueMutex);
    mTaskQueue.push(std::move(task));

    if (!mDrainerScheduled) {
        qemu_bh_schedule(mDrainerBh.get());
        mDrainerScheduled = true;
    }
}

void QemuEventLoopImpl::postImpl(Task task, std::chrono::milliseconds delay) {
    if (mIsShuttingDown) {
        LOG(ERROR) << "Event loop is shutting down, not scheduling task";
        return;
    }

    if (delay == std::chrono::milliseconds::zero()) {
        postImmediately(std::move(task));
        return;
    }

    // The timer will manage its own lifetime via a shared_ptr cycle that is
    // broken when the timer fires.
    auto timer =
            std::make_shared<QemuTimer>(std::move(task), delay, std::chrono::milliseconds(0), this);
    timer->start();
}

std::shared_ptr<EventLoop::Timer> QemuEventLoopImpl::scheduleDelayed(
        Task task, std::chrono::milliseconds delay) {
    if (mIsShuttingDown) {
        return nullptr;
    }
    auto timer =
            std::make_shared<QemuTimer>(std::move(task), delay, std::chrono::milliseconds(0), this);
    timer->start();
    return std::make_shared<ScopedTimer>(timer);
}

std::shared_ptr<EventLoop::Timer> QemuEventLoopImpl::scheduleRepeating(
        Task task, std::chrono::milliseconds initial_delay, std::chrono::milliseconds interval) {
    if (mIsShuttingDown) {
        return nullptr;
    }
    auto timer = std::make_shared<QemuTimer>(std::move(task), initial_delay, interval, this);
    timer->start();
    return std::make_shared<ScopedTimer>(timer);
}

void* QemuEventLoopImpl::getRawLoop() const {
    return nullptr;
}

}  // namespace

// --- Factory Function ---
namespace goldfish::async {

std::unique_ptr<QemuEventLoop> QemuEventLoop::create() {
    // Discover the qemu thread and mark ourselves as running.
    auto loop = std::make_unique<QemuEventLoopImpl>();

    // It is always running..
    loop->setState(LooperStatusEvent::State::RUNNING);
    loop->post([] {});
    return loop;
}

}  // namespace goldfish::async
