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

#include "emulator/hal/plug/test/hal_plug_testing_friend.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/devices/hal_plug_factory.h"
#include "goldfish/devices/hal_plug_to_i_plug_adapter.h"
#include "goldfish/devices/marshalling_hal_socket.h"

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
    MOCK_METHOD(void, SendAsync, (const void* data, size_t size), (override));
    MOCK_METHOD(cable::PlugPtr, SwitchPlug, (cable::PlugPtr newPlug), (override));
    MOCK_METHOD(cable::PlugPtr, UnplugImpl, (), (override));
};

// Mock for the real HalPlug that lives on the client thread.
class MockHalPlug : public HalPlug {
  public:
    MOCK_METHOD(void, OnConnect, (), (override));
    MOCK_METHOD(void, OnReceive, (std::string_view data), (override));
    MOCK_METHOD(void, OnClose, (), (override));

    std::shared_ptr<HalSocket> getSocket() { return Socket(); }
};

class HalPlugAdapterTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mClientLoop = ThreadedEventLoop::Create(LibuvEventLoop::Create());
        mQemuLoop = ThreadedEventLoop::Create(LibuvEventLoop::Create());

        mMockHalPlug = std::make_shared<MockHalPlug>();
        // The SocketPtr owns the mock, but we keep a raw pointer for EXPECT_CALL
        mMockSocket = std::make_unique<MockSocket>();
        mMockSocketPtr = cable::SocketPtr(mMockSocket.get());

        auto f = mQemuLoop->PostAndWait([&] {
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
            EXPECT_CALL(*mMockSocket, UnplugImpl()).WillOnce(Invoke([&]() {
                closed.Notify();
                return nullptr;
            }));
            mClientLoop->Post([this] { mMockHalPlug->getSocket()->Close(); }).IgnoreError();
            closed.WaitForNotificationWithTimeout(absl::Milliseconds(100));
        }

        mQemuLoop->ShutdownAndWait(100ms).IgnoreError();
        mClientLoop->ShutdownAndWait(100ms).IgnoreError();
    }

    void Connect() {
        absl::Notification onConnectCalled;
        EXPECT_CALL(*mMockHalPlug, OnConnect()).WillOnce(Invoke([&]() {
            onConnectCalled.Notify();
        }));

        mClientLoop
                ->Post([this, s = std::move(mMockSocketPtr)]() mutable {
                    auto marshalling_socket =
                            std::make_shared<MarshallingHalSocket>(std::move(s), mQemuLoop.get());
                    HalPlugTesting::EstablishConnection(mMockHalPlug.get(), marshalling_socket);
                    mMockHalPlug->OnConnect();
                })
                .IgnoreError();

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

    EXPECT_CALL(*mMockHalPlug, OnConnect()).WillOnce(Invoke([&]() {
        EXPECT_EQ(std::this_thread::get_id(), mClientLoop->GetId());
        onConnectCalled.Notify();
    }));

    // Simulate a connection..
    mClientLoop
            ->Post([this, s = std::move(mMockSocketPtr)]() mutable {
                auto marshalling_socket =
                        std::make_shared<MarshallingHalSocket>(std::move(s), mQemuLoop.get());
                HalPlugTesting::EstablishConnection(mMockHalPlug.get(), marshalling_socket);
                mMockHalPlug->OnConnect();
            })
            .IgnoreError();

    // We will fail if no notification within 100ms.
    onConnectCalled.WaitForNotificationWithTimeout(absl::Milliseconds(100));
}

TEST_F(HalPlugAdapterTest, OnReceiveIsMarshalledToClientThread) {
    Connect();
    absl::Notification onReceiveCalled;

    // Test will fail if this call was not made.
    EXPECT_CALL(*mMockHalPlug, OnReceive(StrEq("hello"))).WillOnce(Invoke([&](std::string_view) {
        EXPECT_EQ(std::this_thread::get_id(), mClientLoop->GetId());
        onReceiveCalled.Notify();
    }));

    mQemuLoop->Post([&] { mAdapter->OnReceive("hello", 5); }).IgnoreError();

    // We will fail if no notification within 100ms.
    onReceiveCalled.WaitForNotificationWithTimeout(absl::Milliseconds(100));
}

TEST_F(HalPlugAdapterTest, SendIsMarshalledToQemuThread) {
    Connect();
    absl::Notification sendAsyncCalled;
    EXPECT_CALL(*mMockSocket, SendAsync(_, 5)).WillOnce(Invoke([&](const void* data, size_t) {
        EXPECT_EQ(std::this_thread::get_id(), mQemuLoop->GetId());
        EXPECT_EQ(std::string_view(static_cast<const char*>(data), 5), "world");
        sendAsyncCalled.Notify();
    }));

    mClientLoop->Post([&] { mMockHalPlug->getSocket()->Send("world"); }).IgnoreError();
    sendAsyncCalled.WaitForNotificationWithTimeout(absl::Milliseconds(100));
}

TEST_F(HalPlugAdapterTest, OnUnplugIsMarshalledToOnCloseOnClientThread) {
    Connect();
    absl::Notification onCloseCalled;
    absl::Notification unplugImplCalled;

    EXPECT_CALL(*mMockHalPlug, OnClose()).WillOnce(Invoke([&] {
        EXPECT_EQ(std::this_thread::get_id(), mClientLoop->GetId());
        onCloseCalled.Notify();
    }));

    // onUnplug will trigger close(), which will post a task to call unplugImpl.
    // We need to expect that call and wait for it to ensure the async chain completes.
    EXPECT_CALL(*mMockSocket, UnplugImpl()).WillOnce(Invoke([&]() {
        mSocketIsOpen = false;  // Signal to TearDown that we handled the close.
        unplugImplCalled.Notify();
        return nullptr;
    }));

    mQemuLoop->Post([&] { mAdapter->OnUnplug(); }).IgnoreError();

    // Wait for both notifications to ensure the full sequence has executed.
    onCloseCalled.WaitForNotificationWithTimeout(absl::Milliseconds(100));
    unplugImplCalled.WaitForNotificationWithTimeout(absl::Milliseconds(100));
}

// TODO Fix this test is very flakey
TEST_F(HalPlugAdapterTest, DISABLED_CloseIsMarshalledToUnplugImplOnQemuThread) {
    Connect();
    bool callClose = false;
    absl::Notification unplugCalled;
    absl::Notification postedClose;
    EXPECT_CALL(*mMockSocket, UnplugImpl()).WillOnce(Invoke([&]() -> cable::PlugPtr {
        EXPECT_EQ(std::this_thread::get_id(), mQemuLoop->GetId());
        unplugCalled.Notify();
        mSocketIsOpen = false;
        return nullptr;
    }));

    mClientLoop
            ->Post([&] {
                mMockHalPlug->getSocket()->Close();
                callClose = true;
                postedClose.Notify();
            })
            .IgnoreError();
    postedClose.WaitForNotificationWithTimeout(absl::Milliseconds(100));
    unplugCalled.WaitForNotificationWithTimeout(absl::Milliseconds(100));
    ASSERT_TRUE(callClose);
}
