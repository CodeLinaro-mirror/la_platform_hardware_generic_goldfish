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

#include <memory>
#include <thread>

namespace goldfish::async {

ThreadedEventLoop::ThreadedEventLoop(std::unique_ptr<EventLoop> loop) : mLoop(std::move(loop)) {
    run();
}

ThreadedEventLoop::~ThreadedEventLoop() {
    stop();
}

ThreadedEventLoop::ThreadedEventLoop(ThreadedEventLoop&& other) noexcept
        : mRunner(std::move(other.mRunner)), mLoop(std::move(other.mLoop)) {}

ThreadedEventLoop& ThreadedEventLoop::operator=(ThreadedEventLoop&& other) noexcept {
    if (this != &other) {
        // Stop the current thread before moving new resources in.
        stop();
        mRunner = std::move(other.mRunner);
        mLoop = std::move(other.mLoop);
    }
    return *this;
}

void ThreadedEventLoop::run() {
    mRunner = std::thread([this] { mLoop->run(); });
}

void ThreadedEventLoop::stop() {
    mLoop->stop();
    if (mRunner.joinable()) {
        mRunner.join();
    }
}

bool ThreadedEventLoop::isOnLoopThread() {
    return mLoop->isOnLoopThread();
}

absl::Status ThreadedEventLoop::post(Task fn) {
    return mLoop->post(std::move(fn));
}

}  // namespace goldfish::async