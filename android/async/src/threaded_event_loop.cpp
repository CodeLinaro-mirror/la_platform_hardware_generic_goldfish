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
#include "absl/time/time.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace goldfish::async {

ThreadedEventLoop::ThreadedEventLoop(std::unique_ptr<EventLoop> loop, std::string name)
        : mLoop(std::move(loop)), mLooperName(name) {
    (void)run();
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
    mRunner = std::thread([this] {
#if defined(_WIN32)
        SetThreadName(GetCurrentThread(), mLooperName.c_str());
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

absl::Status ThreadedEventLoop::post(Task fn) {
    return mLoop->post(std::move(fn));
}

absl::Status ThreadedEventLoop::post(Task task, std::chrono::milliseconds delay) {
    return mLoop->post(std::move(task), delay);
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