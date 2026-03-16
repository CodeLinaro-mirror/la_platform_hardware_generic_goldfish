
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

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>

#include "absl/log/log.h"

#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/devices/cable/cable.h"

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
    void SendAsync(const void* const data, const size_t size) override {
        const uint8_t* const data8 = static_cast<const uint8_t*>(data);
        storage.insert(storage.end(), data8, data8 + size);
    }

    PlugPtr SwitchPlug(PlugPtr newPlug) override {
        plug.swap(newPlug);
        return newPlug;
    }

    PlugPtr UnplugImpl() override { return {}; }

    void fakeConnected() { plug->OnConnect(); }

  private:
    std::vector<uint8_t> storage;
    PlugPtr plug;
};

class NullPlug : public IPlug {
    void OnConnect() override {}

    bool OnReceive(const void* data, size_t size) override { return true; };

    SocketPtr OnUnplug() override { return nullptr; };
};

SocketPtr fakeConnection(async::EventLoop* event_loop, PlugPtr plug) {
    auto ptr = SocketPtr(new TestSocket(plug));
    (void)event_loop->Post(
            [socket = ptr.get()]() { static_cast<TestSocket*>(socket)->fakeConnected(); });
    return ptr;
}

using namespace std::chrono_literals;

TEST(ConnectionAwaiter, make_fake_connection) {
    auto event_loop = TestEventLoop::Create();
    auto plug = std::make_shared<NullPlug>();
    auto socket = fakeConnection(event_loop.get(), plug);
}

TEST(ConnectionAwaiter, fires_on_connect) {
    bool connected = false;
    auto event_loop = TestEventLoop::Create();
    auto ready = ConnectionAwaiter::RetryUntilConnected(
            event_loop.get(), [&](auto plug) { return fakeConnection(event_loop.get(), plug); },
            [&](SocketPtr sock) { connected = true; }, 10ms);

    event_loop->AdvanceClock(10ms);
    event_loop->RunAll();
    EXPECT_TRUE(connected);
}

TEST(ConnectionAwaiter, tries_to_connect_multiple_times) {
    bool connected = false;
    int invocation = 0;
    auto event_loop = TestEventLoop::Create();
    auto ready = ConnectionAwaiter::RetryUntilConnected(
            event_loop.get(),
            [&](auto plug) {
                invocation++;
                VLOG(1) << "Connection attempt: " << invocation;
                return SocketPtr(new TestSocket(plug));
            },
            [&](SocketPtr sock) { connected = true; }, 10ms);

    event_loop->AdvanceClock(10ms);
    event_loop->AdvanceClock(10ms);
    event_loop->AdvanceClock(10ms);
    event_loop->AdvanceClock(10ms);
    EXPECT_FALSE(connected);
    EXPECT_EQ(invocation, 4);
}

TEST(ConnectionAwaiter, stop_calling_after_connect) {
    bool connected = false;
    int invocation = 0;
    auto event_loop = TestEventLoop::Create();
    auto ready = ConnectionAwaiter::RetryUntilConnected(
            event_loop.get(),
            [&](auto plug) {
                // On the third invocation we will connect.
                invocation++;
                if (invocation == 3) {
                    return fakeConnection(event_loop.get(), plug);
                }
                return SocketPtr(new TestSocket(plug));
            },
            [&](SocketPtr sock) { connected = true; }, 10ms);

    for (int i = 0; i < 4; i++) {
        event_loop->AdvanceClock(10ms);
        event_loop->RunAll();
    }

    EXPECT_TRUE(connected);
    EXPECT_EQ(invocation, 3);
}

}  // namespace devices
}  // namespace goldfish