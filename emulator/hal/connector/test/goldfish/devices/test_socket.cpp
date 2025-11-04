
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
#include "goldfish/devices/test_socket.h"

#include <chrono>

#include "goldfish/vsock/listen.h"

namespace goldfish {

namespace devices {

using namespace std::chrono_literals;

SocketPtr fakeConnection(Looper* looper, PlugPtr plug) {
    auto ptr = SocketPtr(new TestSocket(plug));
    looper->scheduleCallback(
            [socket = ptr.get()]() { static_cast<TestSocket*>(socket)->fakeConnected(); });
    return ptr;
}

void runLooperUntilCompletion(TestLooper& looper, std::chrono::milliseconds deadline) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < deadline) {
        if (looper.runWithTimeoutMs(50) == EWOULDBLOCK) {
            return;
        }
    }
}
}  // namespace devices
}  // namespace goldfish
