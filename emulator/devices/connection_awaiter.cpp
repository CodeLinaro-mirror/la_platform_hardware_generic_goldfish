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

#include "aemu/base/async/RecurrentTask.h"
#include "goldfish/devices/cable/cable.h"

namespace goldfish {
namespace devices {

using android::base::Looper;
using android::base::RecurrentTask;
using cable::IPlug;
using cable::PlugPtr;
using cable::SocketPtr;

ConnectionAwaiter::~ConnectionAwaiter() {
    std::lock_guard<std::mutex> lock(mConnectionMutex);
    mConnectionRetryTask.stopAndWait();
}

void ConnectionAwaiter::onConnect() {
    std::lock_guard<std::mutex> lock(mConnectionMutex);
    mIsConnected = true;
    mConnectionRetryTask.stopAsync();
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
        Looper* looper, CreateConnection createConnection, ConnectionCallback onConnected,
        std::chrono::milliseconds interval) {
    return std::shared_ptr<ConnectionAwaiter>(
            new ConnectionAwaiter(looper, createConnection, onConnected, interval));
}

ConnectionAwaiter::ConnectionAwaiter(Looper* looper, CreateConnection createConnection,
                                     ConnectionCallback onConnected,
                                     std::chrono::milliseconds interval)
    : mCreateConnection(std::move(createConnection)),
      mOnConnected(std::move(onConnected)),
      mConnectionRetryTask(
              looper, [this]() { return attemptConnection(); }, interval)

{
    mConnectionRetryTask.start();
}

bool ConnectionAwaiter::attemptConnection() {
    std::lock_guard<std::mutex> lock(mConnectionMutex);
    if (mIsConnected) {
        return false;
    }
    mSocket = mCreateConnection(this->shared_from_this());
    return true;
}
}  // namespace devices
}  // namespace goldfish
