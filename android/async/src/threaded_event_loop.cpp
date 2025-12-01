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

#include "goldfish/async/threaded_event_loop.h"

#include <future>
#include <memory>
#include <thread>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

#ifdef _WIN32
// clang-format off
// IWYU pragma: begin_keep
#include <windows.h>
#include <processthreadsapi.h>
#include "android/base/system/Win32UnicodeString.h"
// IWYU pragma: end_keep
// clang-format on
#else
#include <pthread.h>
#endif

#include "goldfish/async/event_loop.h"
#include "goldfish/async/libuv_event_loop.h"

namespace goldfish::async {

constexpr absl::Duration kMaxStartTimeout = absl::Milliseconds(100);

class ThreadedEventLoopImpl : public ThreadedEventLoop {
  public:
    /**
     * @brief Constructs a ThreadedEventLoopImpl.
     *
     * The constructor will spin up a new thread and run the EventLoop.
     * it will block and wait until the EventLoop has marked itself as started.
     *
     * @param loop A unique_ptr to the underlying EventLoop implementation that
     * this class will manage and run.
     */
    explicit ThreadedEventLoopImpl(std::unique_ptr<LibuvEventLoop> loop,
                                   std::string name = "AEMU Event Thread");
    ~ThreadedEventLoopImpl() override;

    // --- Prevent Copying ---
    ThreadedEventLoopImpl(const ThreadedEventLoopImpl&) = delete;
    ThreadedEventLoopImpl& operator=(const ThreadedEventLoopImpl&) = delete;

    // --- Prevent Moving ---
    ThreadedEventLoopImpl(ThreadedEventLoopImpl&& other) noexcept = delete;
    ThreadedEventLoopImpl& operator=(ThreadedEventLoopImpl&& other) noexcept = delete;

    std::future<absl::Status> shutdown() override {
        return mLoop->shutdown();
    }

    /**
     * @brief Checks if the caller is on the background event loop thread.
     * @return Delegates the call to the underlying EventLoop.
     */
    bool isOnLoopThread() const override {
        return mLoop->isOnLoopThread();
    }

    std::shared_ptr<Timer> createTimer(Task task) override {
        return mLoop->createTimer(std::move(task));
    }

    void* getRawLoop() override { return mLoop->getRawLoop(); }

    EventLoop* loop() { return mLoop.get(); }

    std::thread::id get_id() const override { return mRunner.get_id(); }

    LooperStatusEvent::State getState() const override {
        return mLoop->getState();
    }

    absl::Status start() override;

  private:
    absl::Status postImmediately(Task task) override {
        return mLoop->postImmediately(std::move(task));
    }

    absl::Status postDelayed(Task task, std::chrono::milliseconds delay) override {
        return mLoop->postDelayed(std::move(task), delay);
    }

    std::thread mRunner;
    std::unique_ptr<LibuvEventLoop> mLoop;
    std::string mLooperName;
    std::unique_ptr<android::base::eventing::ScopedEventCallback<LibuvEventLoop, LooperStatusEvent>>
            mSubscription;
};

ThreadedEventLoopImpl::ThreadedEventLoopImpl(std::unique_ptr<LibuvEventLoop> loop, std::string name)
        : mLoop(std::move(loop)), mLooperName(std::move(name)) {
    mSubscription = android::base::eventing::makeScopedCallback(
            *mLoop, [this](const LooperStatusEvent& event) { this->fireEvent(event); });
}

ThreadedEventLoopImpl::~ThreadedEventLoopImpl() {
    VLOG(1) << "~ThreadedEventLoopImpl";
    auto future = shutdown();
    auto wait = future.wait_for(getTimeout());
    if (wait == std::future_status::ready) {
        auto status = future.get();
        if (!status.ok()) {
            LOG(ERROR) << "Failed to shutdown event loop: " << status;
        }
    } else {
        // There is likely a hung task blocking the loop.
        // Join will hang if the loop has not shutdown. All we can do is crash with an error.
        LOG(FATAL) << "ThreadedEventLoop did not complete shutdown within: " << absl::FromChrono(getTimeout());
    }

    if (mRunner.joinable()) {
        mRunner.join();
    }
}

absl::Status ThreadedEventLoopImpl::start() {
    if (getState() != LooperStatusEvent::State::NOT_STARTED) {
        return absl::FailedPreconditionError(
                "The event loop is automatically run, and has already started.");
    }
    mRunner = std::thread([this] {
#if defined(_WIN32)
        SetThreadDescription(GetCurrentThread(),
                             android::base::Win32UnicodeString(mLooperName).c_str());
#elif defined(__linux__)
        pthread_setname_np(pthread_self(), mLooperName.c_str());
#else
        pthread_setname_np(mLooperName.c_str());
#endif
        auto status = mLoop->run();
        if (!status.ok()) {
            LOG(WARNING) << "Event loop exited with: " << status;
        }
    });
    return absl::OkStatus();
}

std::unique_ptr<ThreadedEventLoop> ThreadedEventLoop::create(std::unique_ptr<LibuvEventLoop> toRun) {
    if (!toRun) {
        LOG(WARNING) << "No looper present";
        return nullptr;
    }

    auto loop = std::make_unique<ThreadedEventLoopImpl>(std::move(toRun));
    absl::Notification isRunning;
    auto waitForRun = android::base::eventing::makeScopedCallback(
            *(loop->loop()), [&isRunning](const LooperStatusEvent& event) {
                VLOG(1) << "Eventloop state transitioned to " << event;
                if (event.state == LooperStatusEvent::State::RUNNING) {
                    isRunning.Notify();
                }
            });

    if (auto status = loop->start(); !status.ok()) {
        LOG(WARNING) << "Failed to start inner loop due to: " << status;
        return nullptr;
    }

    VLOG(1) << "Waiting until the thread is truly running";
    if (!isRunning.WaitForNotificationWithTimeout(kMaxStartTimeout)) {
        LOG(WARNING) << "Eventloop state did not transition to running within " << kMaxStartTimeout;
        return nullptr;
    }

    return loop;
}

}  // namespace goldfish::async
