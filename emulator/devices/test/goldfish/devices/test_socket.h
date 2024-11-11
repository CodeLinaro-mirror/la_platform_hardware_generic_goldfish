
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
#pragma once
#include <chrono>
#include <cstdint>
#include <memory>

#include "aemu/base/async/Looper.h"
#include "aemu/base/testing/TestLooper.h"
#include "goldfish/devices/cable/cable.h"

namespace goldfish {
namespace devices {
using android::base::Looper;
using android::base::TestLooper;

using cable::IPlug;
using cable::ISocket;
using cable::PlugPtr;
using cable::SocketPtr;

using namespace std::chrono_literals;

struct TestSocket : public ISocket {
  public:
    TestSocket(PlugPtr p) : plug(std::move(p)) {}
    void sendAsync(const void* const data, const size_t size) override {
        const uint8_t* const data8 = static_cast<const uint8_t*>(data);
        storage.insert(storage.end(), data8, data8 + size);
    }

    PlugPtr switchPlug(PlugPtr newPlug) override {
        plug.swap(newPlug);
        return newPlug;
    }

    PlugPtr unplugImpl() override { return {}; }

    void fakeConnected() { plug->onConnect(); }

    std::string storage;
    PlugPtr plug;
};

class NullPlug : public IPlug {
    void onConnect() override {}

    bool onReceive(const void* data, size_t size) override { return true; };

    SocketPtr onUnplug() override { return nullptr; };
};

// Create a fake connection.
SocketPtr fakeConnection(Looper* looper, PlugPtr plug = std::make_shared<NullPlug>());

// Finish the testlooper.
void runLooperUntilCompletion(TestLooper& looper, std::chrono::milliseconds deadline = 200ms);

}  // namespace devices
}  // namespace goldfish