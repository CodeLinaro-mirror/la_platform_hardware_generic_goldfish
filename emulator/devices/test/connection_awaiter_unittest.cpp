
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

#include "aemu/base/async/Looper.h"
#include "aemu/base/testing/TestLooper.h"

namespace goldfish {
namespace devices {
using android::base::Looper;
using android::base::RecurrentTask;
using android::base::TestLooper;

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

SocketPtr fakeConnection(Looper* looper, PlugPtr plug) {
    auto ptr = SocketPtr(new TestSocket(plug));
    looper->scheduleCallback(
            [socket = ptr.get()]() { static_cast<TestSocket*>(socket)->fakeConnected(); });
    return ptr;
}

TEST(ConnectionAwaiter, make_fake_connection) {
    TestLooper looper;
    auto plug = std::make_shared<NullPlug>();
    auto socket = fakeConnection(&looper, plug);
}

TEST(ConnectionAwaiter, fires_on_connect) {
    bool connected = false;
    TestLooper looper;
    auto ready = ConnectionAwaiter::retryUntilConnected(
            &looper, [&](auto plug) { return fakeConnection(&looper, plug); },
            [&](SocketPtr sock) { connected = true; }, 10ms);

    looper.runWithTimeoutMs(50);
    EXPECT_TRUE(connected);
}

TEST(ConnectionAwaiter, tries_to_connect_multiple_times) {
    bool connected = false;
    int invocation = 0;
    TestLooper looper;
    auto ready = ConnectionAwaiter::retryUntilConnected(
            &looper,
            [&](auto plug) {
                invocation++;
                return SocketPtr(new TestSocket(plug));
            },
            [&](SocketPtr sock) { connected = true; }, 10ms);

    looper.runWithTimeoutMs(50);
    EXPECT_FALSE(connected);

    // Note our timers are not very accurate..
    EXPECT_LE(invocation, 5);
    EXPECT_GE(invocation, 4);
}

TEST(ConnectionAwaiter, stop_calling_after_connect) {
    bool connected = false;
    int invocation = 0;
    TestLooper looper;
    auto ready = ConnectionAwaiter::retryUntilConnected(
            &looper,
            [&](auto plug) {
                // On the third invocation we will connect.
                invocation++;
                if (invocation == 3) {
                    return fakeConnection(&looper, plug);
                }
                return SocketPtr(new TestSocket(plug));
            },
            [&](SocketPtr sock) { connected = true; }, 10ms);

    looper.runWithTimeoutMs(50);
    EXPECT_TRUE(connected);
    EXPECT_EQ(invocation, 3);
}

// This finds problems in the testlooper..
TEST(ConnectionAwaiter, tsan_thread_test) {
    bool connected = false;
    int invocation = 0;
    TestLooper looper;
    TestLooper looper2;
    auto ready = ConnectionAwaiter::retryUntilConnected(
            &looper,
            [&](auto plug) {
                // On the third invocation we will connect.
                invocation++;
                if (invocation == 3) {
                    return fakeConnection(&looper2, plug);
                }
                return SocketPtr(new TestSocket(plug));
            },
            [&](SocketPtr sock) { connected = true; }, 10ms);

    bool t1done = false;
    std::thread t1([&]() {
        looper.runWithTimeoutMs(50);
        t1done = true;
    });
    std::thread t2([&]() {
        while (!t1done) looper2.runWithTimeoutMs(50);
    });
    t1.join();
    t2.join();

    EXPECT_TRUE(connected);

    // The connect happens on looper2.. It is very well possible that
    // we fire the connect after receceiving more events..
    EXPECT_LE(invocation, 4);
}

}  // namespace devices
}  // namespace goldfish