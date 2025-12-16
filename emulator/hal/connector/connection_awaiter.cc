// Copyright (C) 2024 The Android Open Source Project
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
#include "goldfish/devices/connection_awaiter.h"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>

#include "goldfish/devices/cable/cable.h"

namespace goldfish {
namespace devices {

using cable::IPlug;
using cable::PlugPtr;
using cable::SocketPtr;
namespace async = goldfish::async;

ConnectionAwaiter::~ConnectionAwaiter() {
    std::lock_guard<std::mutex> lock(mConnectionMutex);
    if (mConnectionRetryTask) {
        VLOG(1) << "Cancelling task";
        mConnectionRetryTask->Cancel();
    }
}

void ConnectionAwaiter::onConnect() {
    std::lock_guard<std::mutex> lock(mConnectionMutex);
    VLOG(1) << "Received onConnect: " << (mIsConnected ? "already connected" : "not connected yet");
    if (mIsConnected) {
        return;
    }
    mIsConnected = true;
    if (mConnectionRetryTask) {
        mConnectionRetryTask->Cancel();
        mConnectionRetryTask.reset();
    }
    mOnConnected(std::move(mSocket));
    mSocket = nullptr;
}

bool ConnectionAwaiter::onReceive(const void* data, size_t size) {
    return false;
};

SocketPtr ConnectionAwaiter::onUnplug() {
    std::lock_guard<std::mutex> lock(mConnectionMutex);
    return std::move(mSocket);
}

std::shared_ptr<ConnectionAwaiter> ConnectionAwaiter::retryUntilConnected(
        async::EventLoop* eventLoop, CreateConnection createConnection,
        ConnectionCallback onConnected, std::chrono::milliseconds interval) {
    return std::make_shared<ConnectionAwaiter>(eventLoop, std::move(createConnection),
                                               std::move(onConnected), interval, Private());
}

ConnectionAwaiter::ConnectionAwaiter(async::EventLoop* eventLoop, CreateConnection createConnection,
                                     ConnectionCallback onConnected,
                                     std::chrono::milliseconds interval, Private)
        : mCreateConnection(std::move(createConnection)), mOnConnected(std::move(onConnected)) {
    VLOG(1) << "Scheduling retry task with interval: " << interval;
    mConnectionRetryTask =
            eventLoop->ScheduleRepeating([this]() { attemptConnection(); }, interval, interval);
}

bool ConnectionAwaiter::attemptConnection() {
    std::lock_guard<std::mutex> lock(mConnectionMutex);
    VLOG(2) << "attempting a connection: "
            << (mIsConnected ? "already connected" : "not connected");
    if (mIsConnected) {
        return false;
    }
    mSocket = mCreateConnection(this->shared_from_this());
    return true;
}
}  // namespace devices
}  // namespace goldfish
