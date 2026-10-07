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
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/status/status_matchers.h"
#include "absl/synchronization/notification.h"

#include "android/base/testing/test_temp_dir.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/devices/hal_plug_factory.h"
#include "goldfish/devices/hal_plug_to_i_plug_adapter.h"
#include "goldfish/devices/marshalling_hal_socket.h"
#include "goldfish/devices/test/hal_plug_testing_friend.h"
#include "goldfish/perfetto/perfetto.h"

using namespace goldfish::devices;
using namespace goldfish::async;
using namespace std::chrono_literals;

using ::testing::_;
using ::testing::Invoke;
using ::testing::StrEq;

namespace {

// Mock for the real ISocket that lives on the QEMU thread.
// Following the CABLE ISocket contract, UnplugImpl destroys the socket instance.
class MockSocket : public cable::ISocket {
  public:
    ~MockSocket() override = default;
    MOCK_METHOD(void, SendAsync, (const void* data, size_t size), (override));
    MOCK_METHOD(cable::PlugPtr, SwitchPlug, (cable::PlugPtr newPlug), (override));
    MOCK_METHOD(cable::PlugPtr, OnUnplug, ());

    cable::PlugPtr UnplugImpl() override {
        cable::PlugPtr plug = OnUnplug();
        delete this;
        return plug;
    }
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
        // The SocketPtr exclusively owns the mock via ISocket::Unplugger
        mMockSocket = new MockSocket();
        mMockSocketPtr = cable::SocketPtr(mMockSocket);

        auto f = mQemuLoop->PostAndWait([&] {
            return std::make_shared<HalPlugToIPlugAdapter>(mClientLoop.get(), mMockHalPlug);
        });
        ASSERT_THAT(f, ::absl_testing::IsOk());
        mAdapter = *f;
    }

    void TearDown() override {
        // Ensure cleanup if a test hasn't already closed the socket.
        // This prevents leaks if a test fails before calling close().
        if (mMockHalPlug && mMockHalPlug->getSocket() && mSocketIsOpen) {
            absl::Notification closed;
            EXPECT_CALL(*mMockSocket, OnUnplug()).WillOnce(Invoke([&]() {
                closed.Notify();
                return nullptr;
            }));
            mClientLoop->Post([this] { mMockHalPlug->getSocket()->Close(); }).IgnoreError();
            closed.WaitForNotificationWithTimeout(absl::Seconds(2));
        }

        mAdapter.reset();
        mMockHalPlug.reset();
        mMockSocketPtr.reset();

        mClientLoop->ShutdownAndWait(absl::Seconds(2)).IgnoreError();
        mQemuLoop->ShutdownAndWait(absl::Seconds(2)).IgnoreError();
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
    MockSocket* mMockSocket = nullptr;  // Non-owning
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
    EXPECT_CALL(*mMockSocket, OnUnplug()).WillOnce(Invoke([&]() {
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
    EXPECT_CALL(*mMockSocket, OnUnplug()).WillOnce(Invoke([&]() -> cable::PlugPtr {
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

TEST_F(HalPlugAdapterTest, CoalescesMultipleSendsIntoSingleQemuTask) {
    Connect();

    absl::Notification qemuGate;
    absl::Notification sendAsyncCalled;

    // Block mQemuLoop so it pauses processing queued looper closures
    mQemuLoop->Post([&] { qemuGate.WaitForNotification(); }).IgnoreError();

    // Expect exactly ONE SendAsync call containing the concatenated payload "msg1:msg2:msg3:" (15
    // bytes)
    EXPECT_CALL(*mMockSocket, SendAsync(_, 15)).WillOnce(Invoke([&](const void* data, size_t size) {
        EXPECT_EQ(std::this_thread::get_id(), mQemuLoop->GetId());
        EXPECT_EQ(std::string_view(static_cast<const char*>(data), size), "msg1:msg2:msg3:");
        sendAsyncCalled.Notify();
    }));

    // Issue multiple Send calls while mQemuLoop is gated
    auto socket = mMockHalPlug->getSocket();
    socket->Send("msg1:");
    socket->Send("msg2:");
    socket->Send("msg3:");

    // Release the gate so mQemuLoop processes the single posted flush task
    qemuGate.Notify();

    // Assert SendAsync was called once with the coalesced buffer
    sendAsyncCalled.WaitForNotificationWithTimeout(absl::Milliseconds(100));
}

TEST_F(HalPlugAdapterTest, CoalescesMultipleOnReceivesIntoSingleClientDelivery) {
    Connect();

    absl::Notification clientGate;
    absl::Notification onReceiveCalled;

    // Block mClientLoop so queued delivery tasks wait while QEMU pushes multiple 4 KiB pages.
    mClientLoop->Post([&] { clientGate.WaitForNotification(); }).IgnoreError();

    EXPECT_CALL(*mMockHalPlug, OnReceive(StrEq("page1:page2:page3:")))
            .WillOnce(Invoke([&](std::string_view) {
                EXPECT_EQ(std::this_thread::get_id(), mClientLoop->GetId());
                onReceiveCalled.Notify();
            }));

    mQemuLoop
            ->Post([&] {
                mAdapter->OnReceive("page1:", 6);
                mAdapter->OnReceive("page2:", 6);
                mAdapter->OnReceive("page3:", 6);
            })
            .IgnoreError();

    // Ensure the QEMU loop has processed all three OnReceive calls before releasing mClientLoop.
    auto synced = mQemuLoop->PostAndWait([] {});
    ASSERT_THAT(synced, ::absl_testing::IsOk());

    clientGate.Notify();
    EXPECT_TRUE(onReceiveCalled.WaitForNotificationWithTimeout(absl::Milliseconds(200)));
}

TEST_F(HalPlugAdapterTest, CoalescesInitialSendsAndDispatchesSubsequentSendSeparately) {
    Connect();

    absl::Notification gate1;
    absl::Notification turn1Done;
    absl::Notification turn2Done;

    // Block mQemuLoop for Turn 1
    mQemuLoop->Post([&] { gate1.WaitForNotification(); }).IgnoreError();

    // Call 1: Coalesced payload "part1:part2:" (12 bytes)
    // Call 2: Subsequent payload "part3:" (6 bytes)
    ::testing::Sequence seq;
    EXPECT_CALL(*mMockSocket, SendAsync(_, 12))
            .InSequence(seq)
            .WillOnce(Invoke([&](const void* data, size_t size) {
                EXPECT_EQ(std::string_view(static_cast<const char*>(data), size), "part1:part2:");
                turn1Done.Notify();
            }));

    EXPECT_CALL(*mMockSocket, SendAsync(_, 6))
            .InSequence(seq)
            .WillOnce(Invoke([&](const void* data, size_t size) {
                EXPECT_EQ(std::string_view(static_cast<const char*>(data), size), "part3:");
                turn2Done.Notify();
            }));

    auto socket = mMockHalPlug->getSocket();

    // Issue Turn 1 sends (coalesced)
    socket->Send("part1:");
    socket->Send("part2:");

    // Release gate 1 to execute Turn 1 flush
    gate1.Notify();
    turn1Done.WaitForNotificationWithTimeout(absl::Milliseconds(100));

    // Issue Turn 2 send after Turn 1 completed (sent as a separate payload)
    socket->Send("part3:");
    turn2Done.WaitForNotificationWithTimeout(absl::Milliseconds(100));
}

TEST_F(HalPlugAdapterTest, ReusesSendBufferAcrossFlushes) {
    Connect();

    absl::Notification flush1Done;
    absl::Notification flush2Done;
    const void* flush1Ptr = nullptr;
    const void* flush2Ptr = nullptr;

    constexpr size_t kPayloadSize = 32 * 1024;
    const std::string payload1(kPayloadSize, 'a');
    const std::string payload2(kPayloadSize, 'b');

    ::testing::Sequence seq;
    EXPECT_CALL(*mMockSocket, SendAsync(_, kPayloadSize))
            .InSequence(seq)
            .WillOnce(Invoke([&](const void* data, size_t size) {
                EXPECT_EQ(std::string_view(static_cast<const char*>(data), size), payload1);
                flush1Ptr = data;
                flush1Done.Notify();
            }));

    EXPECT_CALL(*mMockSocket, SendAsync(_, kPayloadSize))
            .InSequence(seq)
            .WillOnce(Invoke([&](const void* data, size_t size) {
                EXPECT_EQ(std::string_view(static_cast<const char*>(data), size), payload2);
                flush2Ptr = data;
                flush2Done.Notify();
            }));

    auto socket = mMockHalPlug->getSocket();
    socket->Send(payload1);
    ASSERT_TRUE(flush1Done.WaitForNotificationWithTimeout(absl::Milliseconds(500)));

    socket->Send(payload2);
    ASSERT_TRUE(flush2Done.WaitForNotificationWithTimeout(absl::Milliseconds(500)));

    EXPECT_NE(flush1Ptr, nullptr);
    EXPECT_EQ(flush1Ptr, flush2Ptr);
}

#if defined(CONFIG_TRACE_GOLDFISH)
bool TraceFileContains(const std::filesystem::path& path, std::string_view needle) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    return content.find(needle) != std::string::npos;
}

TEST_F(HalPlugAdapterTest, EmitsPerfettoTraceEventsForSendAndReceive) {
    goldfish::perfetto::Initialize();
    android::base::TestTempDir tmp_dir("hal_plug_perfetto");
    const std::filesystem::path trace_file = tmp_dir.MakeSubPath("hal_plug.pftrace");

    {
        goldfish::perfetto::EmulatorTracingSession session(trace_file, "async,qemu");
        Connect();

        absl::Notification sendDone;
        absl::Notification recvDone;
        EXPECT_CALL(*mMockSocket, SendAsync(_, _)).WillOnce(Invoke([&](const void*, size_t) {
            sendDone.Notify();
        }));
        EXPECT_CALL(*mMockHalPlug, OnReceive(StrEq("pong"))).WillOnce(Invoke([&](std::string_view) {
            recvDone.Notify();
        }));

        mMockHalPlug->getSocket()->Send("ping");
        mQemuLoop->Post([&] { mAdapter->OnReceive("pong", 4); }).IgnoreError();

        sendDone.WaitForNotificationWithTimeout(absl::Milliseconds(200));
        recvDone.WaitForNotificationWithTimeout(absl::Milliseconds(200));
    }

    EXPECT_TRUE(std::filesystem::exists(trace_file));
    EXPECT_TRUE(TraceFileContains(trace_file, "MarshallingHalSocket::Send"));
    EXPECT_TRUE(TraceFileContains(trace_file, "MarshallingHalSocket::Flush"));
    EXPECT_TRUE(TraceFileContains(trace_file, "HalPlugToIPlugAdapter::OnReceive"));
    EXPECT_TRUE(TraceFileContains(trace_file, "HalPlugToIPlugAdapter::Deliver"));
}
#endif

}  // namespace
