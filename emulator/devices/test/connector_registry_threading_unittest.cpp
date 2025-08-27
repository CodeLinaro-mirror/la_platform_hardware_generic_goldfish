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
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/connector_registry.h"
#include "goldfish/hal/plug/HalPlug.h"

using goldfish::async::LibuvEventLoop;
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

// A mock HalPlug to verify that its methods are called on the correct threads.
class MockHalPlug : public HalPlug {
  public:
    MOCK_METHOD(void, onConnect, (), (override));
    MOCK_METHOD(void, onReceive, (std::string_view data), (override));
    MOCK_METHOD(void, onClose, (), (override));

    // Returns the socket associated with this plug.
    HalSocket* getSocket() { return socket(); }

    std::thread::id onReceiveThreadId;
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

class ConnectorRegistryThreadingTest : public ::testing::Test {
  protected:
    void SetUp() override {
        mQemuLoop = std::make_unique<LibuvEventLoop>();
        mClientLoop = std::make_unique<LibuvEventLoop>();
        mQemuThread = std::thread([this] { (void)mQemuLoop->run(); });
        mClientThread = std::thread([this] { (void)mClientLoop->run(); });
    }

    void TearDown() override {
        // Shut down the event loops and join the threads cleanly.
        mQemuLoop->shutdown(100ms).wait_for(100ms);
        mQemuLoop->stop();
        mClientLoop->shutdown(100ms).wait_for(100ms);
        mClientLoop->stop();
        mQemuThread.join();
        mClientThread.join();
    }

    std::unique_ptr<LibuvEventLoop> mQemuLoop;
    std::unique_ptr<LibuvEventLoop> mClientLoop;
    std::thread mQemuThread;
    std::thread mClientThread;
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

    // --- Test connection ---
    absl::Notification onConnectCalled;
    std::thread::id onConnectThreadId;

    // The HAL device receives an empty message upon connection, a leftover
    // from the vsock protocol.
    EXPECT_CALL(*mockHalPlug, onReceive(""));

    EXPECT_CALL(*mockHalPlug, onConnect()).WillOnce(Invoke([&]() {
        onConnectThreadId = std::this_thread::get_id();
        onConnectCalled.Notify();
    }));

    // Act: Register the HAL device. The factory lambda captures the pre-created
    // mockHalPlug. This is done for convenience to set expectations on the mock
    // object before it's used by other threads.
    registry.registerHalDevice(kDeviceName, mClientLoop.get(), mQemuLoop.get(),
                               [mockHalPlug]() { return mockHalPlug; });

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

    // Act: Simulate a connection request from the QEMU side.
    // it contains the protocol, device name, and possible args for our device.
    // In our case we have no arguments. So the first call our DevicePlug will
    // get is the empty string.
    auto connectionString = absl::StrFormat("pipe:%s:args\0", kDeviceName);
    connectorPlug->onReceive(connectionString.data(), connectionString.size() + 1);

    // Assert: Verify onConnect was called on the client thread.
    ASSERT_TRUE(onConnectCalled.WaitForNotificationWithTimeout(absl::Seconds(1)));
    EXPECT_EQ(onConnectThreadId, mClientThread.get_id());

    // --- Test data flow: QEMU -> Client ---
    {
        absl::Notification onReceiveCalled;
        EXPECT_CALL(*mockHalPlug, onReceive(kHelloFromQemu))
                .WillOnce(Invoke([&](std::string_view /* data */) {
                    mockHalPlug->onReceiveThreadId = std::this_thread::get_id();
                    onReceiveCalled.Notify();
                }));

        // Act: Post a message from the QEMU loop.
        (void)mQemuLoop->post([&] { testSocket.send(kHelloFromQemu); });

        // Assert: Verify onReceive was called on the client thread.
        ASSERT_TRUE(onReceiveCalled.WaitForNotificationWithTimeout(absl::Seconds(1)));
        EXPECT_EQ(mockHalPlug->onReceiveThreadId, mClientThread.get_id());
    }

    // --- Test data flow: Client -> QEMU ---
    {
        absl::Notification sendAsyncCalled;
        EXPECT_CALL(testSocket, sendAsync(_, kWorldFromClient.size()))
                .WillOnce(Invoke([&](const void* data, size_t size) {
                    EXPECT_EQ(std::this_thread::get_id(), mQemuThread.get_id());
                    EXPECT_EQ(std::string_view(static_cast<const char*>(data), size),
                              kWorldFromClient);
                    sendAsyncCalled.Notify();
                }));

        // Act: Post a send request from the client loop.
        (void)mClientLoop->post([&] { mockHalPlug->getSocket()->send(kWorldFromClient); });

        // Assert: Verify sendAsync was called on the QEMU thread.
        ASSERT_TRUE(sendAsyncCalled.WaitForNotificationWithTimeout(absl::Seconds(1)));
    }

    // --- Test disconnection ---
    {
        absl::Notification onCloseCalled;
        EXPECT_CALL(*mockHalPlug, onClose()).WillOnce(Invoke([&]() {
            static int callcount = 0;
            VLOG(1) << "Called: " << callcount++;
            EXPECT_EQ(std::this_thread::get_id(), mClientThread.get_id());

            // Clients will usually close the socket..
            onCloseCalled.Notify();
        }));

        // Act: Unplug the connection from the QEMU loop.
        (void)mQemuLoop->post([&] {
            VLOG(1) << "Going to unplug the adapter";
            testSocket.plug->onUnplug();
        });

        // Assert: Verify onClose was called on the client thread.
        ASSERT_TRUE(onCloseCalled.WaitForNotificationWithTimeout(absl::Seconds(1)));
    }

    // Closing out the socket will call unplug..
    EXPECT_CALL(testSocket, unplugImpl()).Times(1);
    // Let's close out our socket, which will release all our resources as well.
    mockHalPlug->getSocket()->close();
}

}  // namespace devices
}  // namespace goldfish
