/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>

#include "absl/status/status_matchers.h"
#include "absl/synchronization/notification.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/hal/plug/HalPlugFactory.h"
#include "goldfish/hal/plug/HalPlugToIPlugAdapter.h"
#include "goldfish/hal/plug/MarshallingHalSocket.h"
#include "hal_plug_testing_friend.h"

using namespace goldfish::devices;
using namespace goldfish::async;
using namespace std::chrono_literals;

using ::testing::_;
using ::testing::Invoke;
using ::testing::StrEq;

// Mock for the real ISocket that lives on the QEMU thread.
class MockSocket : public cable::ISocket {
  public:
    ~MockSocket() {}
    MOCK_METHOD(void, sendAsync, (const void* data, size_t size), (override));
    MOCK_METHOD(cable::PlugPtr, switchPlug, (cable::PlugPtr newPlug), (override));
    MOCK_METHOD(cable::PlugPtr, unplugImpl, (), (override));
};

// Mock for the real HalPlug that lives on the client thread.
class MockHalPlug : public HalPlug {
  public:
    MOCK_METHOD(void, onConnect, (), (override));
    MOCK_METHOD(void, onReceive, (std::string_view data), (override));
    MOCK_METHOD(void, onClose, (), (override));

    std::shared_ptr<HalSocket> getSocket() { return socket(); }
};

class HalPlugAdapterTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mClientLoop = ThreadedEventLoop::create(LibuvEventLoop::create());
        mQemuLoop = ThreadedEventLoop::create(LibuvEventLoop::create());

        mMockHalPlug = std::make_shared<MockHalPlug>();
        // The SocketPtr owns the mock, but we keep a raw pointer for EXPECT_CALL
        mMockSocket = std::make_unique<MockSocket>();
        mMockSocketPtr = cable::SocketPtr(mMockSocket.get());

        auto f = mQemuLoop->postAndWait([&] {
            return std::make_shared<HalPlugToIPlugAdapter>(mClientLoop.get(), mMockHalPlug);
        });
        ASSERT_THAT(f, ::absl_testing::IsOk());
        mAdapter = *f;
    }

    void TearDown() override {
        // Ensure cleanup if a test hasn't already closed the socket.
        // This prevents leaks if a test fails before calling close().
        if (mMockHalPlug->getSocket() && mSocketIsOpen) {
            absl::Notification closed;
            EXPECT_CALL(*mMockSocket, unplugImpl()).WillOnce(Invoke([&]() {
                closed.Notify();
                return nullptr;
            }));
            mClientLoop->post([this] { mMockHalPlug->getSocket()->close(); });
            closed.WaitForNotificationWithTimeout(absl::Milliseconds(100));
        }

        mQemuLoop->shutdownAndWait(100ms);
        mClientLoop->shutdownAndWait(100ms);
    }

    void connect() {
        absl::Notification onConnectCalled;
        EXPECT_CALL(*mMockHalPlug, onConnect()).WillOnce(Invoke([&]() {
            onConnectCalled.Notify();
        }));

        mClientLoop->post([this, s = std::move(mMockSocketPtr)]() mutable {
            auto marshallingSocket =
                    std::make_shared<MarshallingHalSocket>(std::move(s), mQemuLoop.get());
            HalPlugTesting::establishConnection(mMockHalPlug.get(), marshallingSocket);
            mMockHalPlug->onConnect();
        });

        onConnectCalled.WaitForNotificationWithTimeout(absl::Milliseconds(100));
    }

    std::unique_ptr<ThreadedEventLoop> mQemuLoop;
    std::unique_ptr<ThreadedEventLoop> mClientLoop;
    std::shared_ptr<MockHalPlug> mMockHalPlug;
    cable::SocketPtr mMockSocketPtr;
    std::unique_ptr<MockSocket> mMockSocket;  // Non-owning
    bool mSocketIsOpen = true;
    std::shared_ptr<HalPlugToIPlugAdapter> mAdapter;
};

TEST_F(HalPlugAdapterTest, OnConnectIsMarshalledToClientThread) {
    absl::Notification onConnectCalled;

    EXPECT_CALL(*mMockHalPlug, onConnect()).WillOnce(Invoke([&]() {
        EXPECT_EQ(std::this_thread::get_id(), mClientLoop->get_id());
        onConnectCalled.Notify();
    }));

    // Simulate a connection..
    mClientLoop->post([this, s = std::move(mMockSocketPtr)]() mutable {
        auto marshallingSocket =
                std::make_shared<MarshallingHalSocket>(std::move(s), mQemuLoop.get());
        HalPlugTesting::establishConnection(mMockHalPlug.get(), marshallingSocket);
        mMockHalPlug->onConnect();
    });

    // We will fail if no notification within 100ms.
    onConnectCalled.WaitForNotificationWithTimeout(absl::Milliseconds(100));
}

TEST_F(HalPlugAdapterTest, OnReceiveIsMarshalledToClientThread) {
    connect();
    absl::Notification onReceiveCalled;

    // Test will fail if this call was not made.
    EXPECT_CALL(*mMockHalPlug, onReceive(StrEq("hello"))).WillOnce(Invoke([&](std::string_view) {
        EXPECT_EQ(std::this_thread::get_id(), mClientLoop->get_id());
        onReceiveCalled.Notify();
    }));

    mQemuLoop->post([&] { mAdapter->onReceive("hello", 5); });

    // We will fail if no notification within 100ms.
    onReceiveCalled.WaitForNotificationWithTimeout(absl::Milliseconds(100));
}

TEST_F(HalPlugAdapterTest, SendIsMarshalledToQemuThread) {
    connect();
    absl::Notification sendAsyncCalled;
    EXPECT_CALL(*mMockSocket, sendAsync(_, 5)).WillOnce(Invoke([&](const void* data, size_t) {
        EXPECT_EQ(std::this_thread::get_id(), mQemuLoop->get_id());
        EXPECT_EQ(std::string_view(static_cast<const char*>(data), 5), "world");
        sendAsyncCalled.Notify();
    }));

    mClientLoop->post([&] { mMockHalPlug->getSocket()->send("world"); });
    sendAsyncCalled.WaitForNotificationWithTimeout(absl::Milliseconds(100));
}

TEST_F(HalPlugAdapterTest, OnUnplugIsMarshalledToOnCloseOnClientThread) {
    connect();
    absl::Notification onCloseCalled;
    absl::Notification unplugImplCalled;

    EXPECT_CALL(*mMockHalPlug, onClose()).WillOnce(Invoke([&] {
        EXPECT_EQ(std::this_thread::get_id(), mClientLoop->get_id());
        onCloseCalled.Notify();
    }));

    // onUnplug will trigger close(), which will post a task to call unplugImpl.
    // We need to expect that call and wait for it to ensure the async chain completes.
    EXPECT_CALL(*mMockSocket, unplugImpl()).WillOnce(Invoke([&]() {
        mSocketIsOpen = false;  // Signal to TearDown that we handled the close.
        unplugImplCalled.Notify();
        return nullptr;
    }));

    mQemuLoop->post([&] { mAdapter->onUnplug(); });

    // Wait for both notifications to ensure the full sequence has executed.
    onCloseCalled.WaitForNotificationWithTimeout(absl::Milliseconds(100));
    unplugImplCalled.WaitForNotificationWithTimeout(absl::Milliseconds(100));
}
