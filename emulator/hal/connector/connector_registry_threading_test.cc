// Copyright 2025 The Android Open Source Project
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
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>

#include "absl/strings/str_format.h"
#include "absl/synchronization/notification.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/connector_registry_impl.h"
#include "goldfish/devices/internal/hal_plug.h"

using goldfish::async::EventLoop;
using goldfish::async::LibuvEventLoop;
using goldfish::async::ThreadedEventLoop;
using goldfish::devices::ConnectorRegistry;
using goldfish::devices::HalPlug;
using goldfish::devices::HalSocket;
using goldfish::devices::cable::PlugPtr;
using goldfish::devices::cable::SocketPtr;
using ::testing::_;
using ::testing::Invoke;
using namespace std::chrono_literals;

namespace goldfish {
namespace devices {

// Helper struct to manage assertions for asynchronous operations.
struct AsyncAssertion {
    absl::Notification notified;
    std::thread::id thread_id;

    void notify() {
        thread_id = std::this_thread::get_id();
        notified.Notify();
    }

    bool wait() { return notified.WaitForNotificationWithTimeout(absl::Seconds(1)); }
};

// A mock HalPlug to verify that its methods are called on the correct threads.
class MockHalPlug : public HalPlug {
  public:
    MOCK_METHOD(void, onConnect, (), (override));
    MOCK_METHOD(void, onReceive, (std::string_view data), (override));
    MOCK_METHOD(void, onClose, (), (override));

    // Returns the socket associated with this plug.
    std::shared_ptr<HalSocket> getSocket() { return socket(); }
};

// A mock socket to simulate the QEMU side of the connection.
class MockSocket : public cable::ISocket {
  public:
    MOCK_METHOD(cable::PlugPtr, unplugImpl, (), (override));
    MOCK_METHOD(void, sendAsync, (const void* data, size_t size), (override));

    // Simulates sending data from the QEMU side to the client.
    void send(std::string_view data) { plug->onReceive(data.data(), data.size()); }

    PlugPtr switchPlug(PlugPtr newPlug) override {
        plug.swap(newPlug);
        return plug;
    }

    PlugPtr plug;
};

// The tests in this file are exercising the ConnectorRegistry, which can
// involve multiple threads for QEMU and guest-side components. We use
// two event loops (mQemuLoop and mClientLoop) to simulate this environment.
//
// To avoid race conditions and flaky tests, it is critical to follow a
// strict "arrange, act, assert" pattern.
//
// 1. **Arrange:** All mocks, expectations (e.g., EXPECT_CALL), and callbacks
//    must be set up *before* the registry starts listening or any connection
//    attempts are made. This ensures that when the event loops start
//    processing events, all the necessary handlers are already in place.
//
// 2. **Act:** The action is typically simulating a guest connection or sending
//    data, which will trigger events that are processed on the event loop
//    threads.
//
// 3. **Assert:** We use synchronization primitives like absl::Notification to
//    wait for asynchronous operations to complete and then assert on the
//    results. This is crucial for verifying behavior that happens on a
//    different thread from the main test thread.
class ConnectorRegistryThreadingTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mClientLoop = ThreadedEventLoop::Create(LibuvEventLoop::Create());
        mQemuLoop = ThreadedEventLoop::Create(LibuvEventLoop::Create());
    }

    std::unique_ptr<ThreadedEventLoop> mQemuLoop;
    std::unique_ptr<ThreadedEventLoop> mClientLoop;
};

// This test verifies the threading model of the ConnectorRegistry.
// It ensures that:
// 1. The HalPlug's onConnect, onReceive, and onClose methods are called on the
//    client thread.
// 2. Data can be sent from the QEMU thread to the client thread.
// 3. Data can be sent from the client thread to the QEMU thread.
TEST_F(ConnectorRegistryThreadingTest, HalDeviceCallbacksAreOnClientThread) {
    ConnectorRegistry registry;
    PlugPtr connectorPlug;

    // Arrange: Create mocks and set up test variables.
    auto mockHalPlug = std::make_shared<::testing::StrictMock<MockHalPlug>>();
    testing::StrictMock<MockSocket> testSocket;
    const std::string kDeviceName = "test-hal";
    const std::string kHelloFromQemu = "Hello";
    const std::string kWorldFromClient = "world";

    // --- Setup phase, we register all the expectations
    // we do this now to make sure we don't run into any concurrency issues
    AsyncAssertion connectAssertion;
    AsyncAssertion receiveAssertion;
    AsyncAssertion closeAssertion;
    AsyncAssertion sendAsyncAssertion;

    // The HAL device receives an empty message upon connection, a leftover
    // from the vsock protocol.
    EXPECT_CALL(*mockHalPlug, onReceive(""));
    EXPECT_CALL(*mockHalPlug, onConnect()).WillOnce(Invoke([&]() { connectAssertion.notify(); }));

    EXPECT_CALL(*mockHalPlug, onReceive(kHelloFromQemu)).WillOnce(Invoke([&](std::string_view) {
        receiveAssertion.notify();
    }));

    EXPECT_CALL(testSocket, sendAsync(_, kWorldFromClient.size()))
            .WillOnce(Invoke([&](const void* data, size_t size) {
                EXPECT_EQ(std::string_view(static_cast<const char*>(data), size), kWorldFromClient);
                sendAsyncAssertion.notify();
            }));

    EXPECT_CALL(testSocket, unplugImpl()).Times(1);
    EXPECT_CALL(*mockHalPlug, onClose()).WillOnce(Invoke([&]() {
        EXPECT_EQ(std::this_thread::get_id(), mClientLoop->GetId());
        closeAssertion.notify();
    }));

    // Act: Register the HAL device. The factory lambda captures the pre-created
    // mockHalPlug. This is done for convenience to set expectations on the mock
    // object before it's used by other threads.
    registry.registerHalDevice(kDeviceName, mClientLoop.get(), mQemuLoop.get(),
                               [mockHalPlug](std::string_view /*args*/) { return mockHalPlug; });

    // Act: Start listening for connections. When a connection occurs, the
    // provided lambda will be called, giving us the connectorPlug. This
    // is a plug that normally is connected to a vsock server port.
    // The plug attached to this socket is the "Connector" plug
    // (@see goldfish/devices/Connector.h). The connector's role is to swap
    // out the "real" plug depending on the protocol requested by the guest.
    auto listenCallback = [&](HostPortListener listenFn) {
        connectorPlug = std::get<PlugPtr>(listenFn(SocketPtr(&testSocket)));
        return true;
    };
    registry.listen(listenCallback);

    // --- Test connection ---
    {
        // Act: Simulate a connection request from the QEMU side.
        // it contains the protocol, device name, and possible args for our device.
        // In our case we have no arguments. So the first call our DevicePlug will
        // get is the empty string.
        auto connectionString = absl::StrFormat("pipe:%s:args\0", kDeviceName);
        connectorPlug->onReceive(connectionString.data(), connectionString.size() + 1);

        // Assert: Verify onConnect was called on the client thread.
        ASSERT_TRUE(connectAssertion.wait());
        EXPECT_EQ(connectAssertion.thread_id, mClientLoop->GetId());
    }

    // --- Test data flow: QEMU -> Client ---
    {
        // Act: Post a message from the QEMU loop.
        (void)mQemuLoop->Post([&] { testSocket.send(kHelloFromQemu); });

        // Assert: Verify onReceive was called on the client thread.
        ASSERT_TRUE(receiveAssertion.wait());
        EXPECT_EQ(receiveAssertion.thread_id, mClientLoop->GetId());
    }

    // --- Test data flow: Client -> QEMU ---
    {
        // Act: Post a send request from the client loop.
        (void)mClientLoop->Post([&] { mockHalPlug->getSocket()->send(kWorldFromClient); });

        // Assert: Verify sendAsync was called on the QEMU thread.
        ASSERT_TRUE(sendAsyncAssertion.wait());
        EXPECT_EQ(sendAsyncAssertion.thread_id, mQemuLoop->GetId());
    }

    // --- Test disconnection ---
    {
        // Act: Unplug the connection from the QEMU loop.
        (void)mQemuLoop->Post([&] {
            VLOG(1) << "Going to unplug the adapter";
            testSocket.plug->onUnplug();
        });

        // Assert: Verify onClose was called on the client thread.
        ASSERT_TRUE(closeAssertion.wait());
        EXPECT_EQ(closeAssertion.thread_id, mClientLoop->GetId());
    }

    // Make sure we don't have live sockets on our loops.
    (void)mQemuLoop->ShutdownAndWait(100ms);
    (void)mClientLoop->ShutdownAndWait(100ms);
}

}  // namespace devices
}  // namespace goldfish
