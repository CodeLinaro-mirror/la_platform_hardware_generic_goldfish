
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

#include "absl/log/check.h"
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
    TestSocket(const uint32_t id, PlugPtr p) : id_(id), plug_(std::move(p)) {}
    void SendAsync(const void* const data, const size_t size) override {
        const uint8_t* const data8 = static_cast<const uint8_t*>(data);
        storage_.insert(storage_.end(), data8, data8 + size);
    }

    PlugPtr SwitchPlug(PlugPtr newPlug) override {
        plug_.swap(newPlug);
        return newPlug;
    }

    PlugPtr UnplugImpl() override { return std::move(plug_); }

    void fakeConnected() { plug_->OnConnect(); }

    bool operator<(const TestSocket& rhs) const { return id_ < rhs.id_; }

  private:
    const uint32_t id_;
    std::vector<uint8_t> storage_;
    PlugPtr plug_;
};

class TestSocketManager {
  public:
    ~TestSocketManager() {
        for (const TestSocket& ts : sockets_) {
            if (const PlugPtr p = const_cast<TestSocket&>(ts).UnplugImpl()) {
                p->OnUnplug();
            }
        }
    }

    SocketPtr Accept(PlugPtr p) {
        const auto [where, inserted] = sockets_.emplace(++idGenerator_, std::move(p));
        CHECK(inserted);
        return SocketPtr(&const_cast<TestSocket&>(*where));
    }

  private:
    std::set<TestSocket> sockets_;
    uint32_t idGenerator_ = 0;
};

class NullPlug : public IPlug {
    void OnConnect() override {}

    bool OnReceive(const void* data, size_t size) override { return true; };

    SocketPtr OnUnplug() override { return nullptr; };
};

SocketPtr fakeConnection(async::EventLoop* event_loop, TestSocketManager& tsm, PlugPtr plug) {
    auto ptr = tsm.Accept(std::move(plug));
    (void)event_loop->Post(
            [socket = ptr.get()]() { static_cast<TestSocket*>(socket)->fakeConnected(); });
    return ptr;
}

using namespace std::chrono_literals;

TEST(ConnectionAwaiter, make_fake_connection) {
    auto event_loop = TestEventLoop::Create();
    TestSocketManager tsm;

    auto plug = std::make_shared<NullPlug>();
    auto socket = fakeConnection(event_loop.get(), tsm, plug);
}

TEST(ConnectionAwaiter, fires_on_connect) {
    auto event_loop = TestEventLoop::Create();
    TestSocketManager tsm;

    bool connected = false;
    auto ready = ConnectionAwaiter::RetryUntilConnected(
            event_loop.get(),
            [&](auto plug) { return fakeConnection(event_loop.get(), tsm, plug); },
            [&](SocketPtr sock) { connected = true; }, 10ms);

    event_loop->AdvanceClock(10ms);
    event_loop->RunAll();
    EXPECT_TRUE(connected);
}

TEST(ConnectionAwaiter, tries_to_connect_multiple_times) {
    auto event_loop = TestEventLoop::Create();
    TestSocketManager tsm;

    bool connected = false;
    int invocation = 0;
    auto ready = ConnectionAwaiter::RetryUntilConnected(
            event_loop.get(),
            [&](auto plug) {
                invocation++;
                VLOG(1) << "Connection attempt: " << invocation;
                return tsm.Accept(std::move(plug));
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
    auto event_loop = TestEventLoop::Create();
    TestSocketManager tsm;

    bool connected = false;
    int invocation = 0;
    auto ready = ConnectionAwaiter::RetryUntilConnected(
            event_loop.get(),
            [&](auto plug) {
                // On the third invocation we will connect.
                invocation++;
                if (invocation == 3) {
                    return fakeConnection(event_loop.get(), tsm, std::move(plug));
                }
                return tsm.Accept(std::move(plug));
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