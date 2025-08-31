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

#include <goldfish/async/event_loop.h>

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
#include "aemu/base/system/Win32UnicodeString.h"
// IWYU pragma: end_keep
// clang-format on
#else
#include <pthread.h>
#endif

namespace goldfish::async {

constexpr absl::Duration kMaxStartTimeout = absl::Milliseconds(100);

ThreadedEventLoop::ThreadedEventLoop(std::unique_ptr<EventLoop> loop, std::string name)
        : mLoop(std::move(loop)), mLooperName(std::move(name)) {
    mSubscription = android::base::eventing::makeScopedCallback(
            *mLoop, [this](const LooperStatusEvent& event) { this->fireEvent(event); });
    absl::Notification isRunning;
    auto waitForRun = android::base::eventing::makeScopedCallback(
            *mLoop, [&isRunning](const LooperStatusEvent& event) {
                VLOG(1) << "Eventloop state transitioned to " << event;
                if (event.state == LooperStatusEvent::State::RUNNING) {
                    isRunning.Notify();
                }
            });

    (void)run();

    VLOG(1) << "Waiting until the thread is truly running";
    if (!isRunning.WaitForNotificationWithTimeout(kMaxStartTimeout)) {
        LOG(WARNING) << "Eventloop state did not transition to running within " << kMaxStartTimeout;
    }
}

ThreadedEventLoop::~ThreadedEventLoop() {
    VLOG(1) << "~ThreadedEventLoop";
    auto future = shutdown(getTimeout());
    auto wait = future.wait_for(getTimeout());
    if (wait == std::future_status::ready) {
        auto status = future.get();
        if (!status.ok()) {
            LOG(ERROR) << "Failed to shutdown event loop: " << status;
        }
    } else {
        LOG(ERROR) << "Did not complete shutdown within: " << absl::FromChrono(getTimeout());
    }

    stop();
}

absl::Status ThreadedEventLoop::run() {
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

std::future<absl::Status> ThreadedEventLoop::shutdown(std::chrono::milliseconds timeout) {
    return mLoop->shutdown(timeout);
}

void ThreadedEventLoop::stop() {
    mLoop->stop();
    if (mRunner.joinable()) {
        mRunner.join();
    }
}

bool ThreadedEventLoop::isOnLoopThread() const {
    return mLoop->isOnLoopThread();
}

void ThreadedEventLoop::postImpl(Task task, std::chrono::milliseconds delay) {
    mLoop->post(std::move(task), delay);
}

std::shared_ptr<EventLoop::Timer> ThreadedEventLoop::scheduleDelayed(
        Task task, std::chrono::milliseconds delay) {
    return mLoop->scheduleDelayed(std::move(task), delay);
}

std::shared_ptr<EventLoop::Timer> ThreadedEventLoop::scheduleRepeating(
        Task task, std::chrono::milliseconds initial_delay, std::chrono::milliseconds interval) {
    return mLoop->scheduleRepeating(std::move(task), initial_delay, interval);
}

}  // namespace goldfish::async