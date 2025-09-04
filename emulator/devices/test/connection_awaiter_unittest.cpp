
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

#include <goldfish/devices/cable/cable.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

#include "absl/log/log.h"

#include "goldfish/async/testing/test_event_loop.h"

namespace goldfish {
namespace devices {
namespace async = goldfish::async;
using async::testing::TestEventLoop;

using cable::IPlug;
using cable::ISocket;
using cable::PlugPtr;
using cable::SocketPtr;

using namespace std::chrono_literals;

class TestSocket : public ISocket {
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

  private:
    std::vector<uint8_t> storage;
    PlugPtr plug;
};

class NullPlug : public IPlug {
    void onConnect() override {}

    bool onReceive(const void* data, size_t size) override { return true; };

    SocketPtr onUnplug() override { return nullptr; };
};

SocketPtr fakeConnection(async::EventLoop* eventLoop, PlugPtr plug) {
    auto ptr = SocketPtr(new TestSocket(plug));
    (void)eventLoop->post(
            [socket = ptr.get()]() { static_cast<TestSocket*>(socket)->fakeConnected(); });
    return ptr;
}

using namespace std::chrono_literals;

TEST(ConnectionAwaiter, make_fake_connection) {
    auto eventLoop = TestEventLoop::create();
    auto plug = std::make_shared<NullPlug>();
    auto socket = fakeConnection(eventLoop.get(), plug);
}

TEST(ConnectionAwaiter, fires_on_connect) {
    bool connected = false;
    auto eventLoop = TestEventLoop::create();
    auto ready = ConnectionAwaiter::retryUntilConnected(
            eventLoop.get(), [&](auto plug) { return fakeConnection(eventLoop.get(), plug); },
            [&](SocketPtr sock) { connected = true; }, 10ms);

    eventLoop->advanceClock(10ms);
    eventLoop->runAll();
    EXPECT_TRUE(connected);
}

TEST(ConnectionAwaiter, tries_to_connect_multiple_times) {
    bool connected = false;
    int invocation = 0;
    auto eventLoop = TestEventLoop::create();
    auto ready = ConnectionAwaiter::retryUntilConnected(
            eventLoop.get(),
            [&](auto plug) {
                invocation++;
                VLOG(1) << "Connection attempt: " << invocation;
                return SocketPtr(new TestSocket(plug));
            },
            [&](SocketPtr sock) { connected = true; }, 10ms);

    eventLoop->advanceClock(10ms);
    eventLoop->advanceClock(10ms);
    eventLoop->advanceClock(10ms);
    eventLoop->advanceClock(10ms);
    EXPECT_FALSE(connected);
    EXPECT_EQ(invocation, 4);
}

TEST(ConnectionAwaiter, stop_calling_after_connect) {
    bool connected = false;
    int invocation = 0;
    auto eventLoop = TestEventLoop::create();
    auto ready = ConnectionAwaiter::retryUntilConnected(
            eventLoop.get(),
            [&](auto plug) {
                // On the third invocation we will connect.
                invocation++;
                if (invocation == 3) {
                    return fakeConnection(eventLoop.get(), plug);
                }
                return SocketPtr(new TestSocket(plug));
            },
            [&](SocketPtr sock) { connected = true; }, 10ms);

    for (int i = 0; i < 4; i++) {
        eventLoop->advanceClock(10ms);
        eventLoop->runAll();
    }

    EXPECT_TRUE(connected);
    EXPECT_EQ(invocation, 3);
}

}  // namespace devices
}  // namespace goldfish