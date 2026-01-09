

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
#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <memory>

#include "emulator/hal/plug/test/fake_vsock.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/connector_registry_impl.h"

namespace goldfish {

using async::EventLoop;
using async::LibuvEventLoop;
using async::ThreadedEventLoop;
using devices::cable::IPlug;
using devices::cable::ISocket;
using devices::cable::PlugPtr;
using devices::cable::SocketPtr;
using namespace std::string_view_literals;

namespace {
struct TestDevice : public IPlug {
    TestDevice(SocketPtr socket, bool isQemud, std::string_view args)
            : mSocket(std::move(socket))
            , mIsQemud(isQemud)
            , mArgs(std::string(args.begin(), args.end())) {}

    static constexpr std::string_view serviceName = "TestDevice"sv;

    SocketPtr OnUnplug() override { return std::move(mSocket); }

    bool OnReceive(const void* data, size_t size) override {
        mData.append(static_cast<const char*>(data), size);
        return true;
    }

    bool SupportsLoadingFromSnapshot() const override { return true; }

    TypeId GetSnapshotTypeId() const override {
        using namespace std::string_literals;
        return "TestDevice"s;
    }

    bool SaveStateToSnapshot(archive::IWriter& writer) const override {
        writer << mIsQemud << mArgs << mData;
        return true;
    }

    SocketPtr mSocket;
    const bool mIsQemud;
    std::string mArgs;
    std::string mData;
};

struct TestSocket : public ISocket {
    void SendAsync(const void* data, size_t size) override {}

    PlugPtr SwitchPlug(PlugPtr newPlug) override {
        plug.swap(newPlug);
        return newPlug;
    }

    PlugPtr UnplugImpl() override { return std::move(plug); }

    bool send(const std::string_view data) { return plug->OnReceive(data.data(), data.size()); }

    PlugPtr plug;
};

struct TestHalDevice : public devices::HalPlug {
    using HalSocket = devices::HalSocket;

    void OnConnect() override { mConnectedPromise.set_value(true); }
    void OnClose() override { mClosedPromise.set_value(true); }
    void OnReceive(std::string_view data) override {}

    std::future<bool> connected() { return mConnectedPromise.get_future(); }
    std::future<bool> closed() { return mClosedPromise.get_future(); }

    void close() { Socket()->Close(); }
    static constexpr std::string_view serviceName = "TestHalDevice"sv;

  private:
    std::promise<bool> mConnectedPromise;
    std::promise<bool> mClosedPromise;
};
}  // namespace

static bool gListenCalled = false;
static TestSocket* gTestSocket;

// Custom vsock::listen implementation for unit tests.
bool fake_vsock_listen(const uint32_t hostPort, devices::HostPortListener listener) {
    if (gListenCalled) {
        return false;
    }
    gListenCalled = true;
    gTestSocket = new TestSocket();
    gTestSocket->plug = std::get<PlugPtr>(listener(SocketPtr(gTestSocket)));
    return true;
}

namespace devices {

class ConnectorRegistryTest : public ::testing::Test {
  public:
    ConnectorRegistryTest() {}

    void SetUp() override {
        vsock::set_fake_listen_fn(fake_vsock_listen);
        listenCalled = false;
        gListenCalled = false;
        gTestSocket = nullptr;

        mQemuLoop = ThreadedEventLoop::Create(LibuvEventLoop::Create());
        mClientLoop = ThreadedEventLoop::Create(LibuvEventLoop::Create());
    }

    void TearDown() override {
        using namespace std::chrono_literals;
        if (gTestSocket) {
            delete gTestSocket;
        }
        mClientLoop.reset();
        mQemuLoop.reset();
    }

  protected:
    ConnectorRegistry registry;
    bool listenCalled = false;

    // Mock ListenFn for testing
    bool mockListenFn(HostPortListener listener) {
        listenCalled = true;
        return true;  // Simulate successful listening
    }

    std::unique_ptr<EventLoop> mQemuLoop;
    std::unique_ptr<EventLoop> mClientLoop;
};

TEST_F(ConnectorRegistryTest, ListenSuccess) {
    ASSERT_TRUE(registry.listen(1234));
    ASSERT_TRUE(gListenCalled);
}

TEST_F(ConnectorRegistryTest, ListenOnlyOnce) {
    ASSERT_TRUE(registry.listen(1234));
    ASSERT_FALSE(registry.listen(1234));
}

TEST_F(ConnectorRegistryTest, ListenFnSuccess) {
    ASSERT_TRUE(
            registry.listen([this](HostPortListener listener) { return mockListenFn(listener); }));
    ASSERT_TRUE(listenCalled);
}

TEST_F(ConnectorRegistryTest, RegisterQemuDeviceBeforeListen) {
    ASSERT_TRUE(registry.registerQemuDevice("device1", [](auto, auto, auto) { return nullptr; }));
}

TEST_F(ConnectorRegistryTest, RegisterDeviceBeforeListen) {
    ASSERT_TRUE(registry.registerDevice("device2", [](auto, auto, auto) { return nullptr; }));
}

TEST_F(ConnectorRegistryTest, RegisterQemuDeviceAfterListen) {
    registry.listen(1234);  // Call listen first
    ASSERT_FALSE(registry.registerQemuDevice("device3", [](auto, auto, auto) { return nullptr; }));
}

TEST_F(ConnectorRegistryTest, RegisterDeviceAfterListen) {
    registry.listen(1234);  // Call listen first
    ASSERT_FALSE(registry.registerDevice("device4", [](auto, auto, auto) { return nullptr; }));
}

TEST_F(ConnectorRegistryTest, RegisteredDeviceIsAvailable) {
#ifdef _WIN32
    // TODO(whollins,b/449212254): Fix this.
    GTEST_SKIP() << "currently broken on Windows";
#endif
    using namespace std::literals;

    bool standardDeviceCreated = false;
    bool qemuDeviceCreated = false;
    registry.registerDevice(
            "TestDevice", [&](cable::SocketPtr socket, const std::shared_ptr<PingTopic>& pingTopic,
                              std::string_view args) {
                standardDeviceCreated = true;
                return std::make_shared<TestDevice>(std::move(socket), false, args);
            });
    registry.registerQemuDevice(
            "TestDevice", [&](cable::SocketPtr socket, const std::shared_ptr<PingTopic>& pingTopic,
                              std::string_view args) {
                qemuDeviceCreated = true;
                return std::make_shared<TestDevice>(std::move(socket), false, args);
            });
    registry.listen(1234);  // Call listen first
    EXPECT_TRUE(gTestSocket->send("pipe:TestDe"sv));
    EXPECT_TRUE(gTestSocket->send("vice:args\0"sv));
    EXPECT_TRUE(standardDeviceCreated);
    EXPECT_FALSE(qemuDeviceCreated);
}

TEST_F(ConnectorRegistryTest, RegisteredQemuDeviceIsAvailable) {
#ifdef _WIN32
    // TODO(whollins,b/449212254): Fix this.
    GTEST_SKIP() << "currently broken on Windows";
#endif
    using namespace std::literals;

    bool standardDeviceCreated = false;
    bool qemuDeviceCreated = false;
    registry.registerDevice(
            "TestDevice", [&](cable::SocketPtr socket, const std::shared_ptr<PingTopic>& pingTopic,
                              std::string_view args) {
                standardDeviceCreated = true;
                return std::make_shared<TestDevice>(std::move(socket), false, args);
            });
    registry.registerQemuDevice(
            "TestDevice", [&](cable::SocketPtr socket, const std::shared_ptr<PingTopic>& pingTopic,
                              std::string_view args) {
                qemuDeviceCreated = true;
                return std::make_shared<TestDevice>(std::move(socket), false, args);
            });
    registry.listen(1234);  // Call listen first
    EXPECT_TRUE(gTestSocket->send("pipe:qemud:TestDe"sv));
    EXPECT_TRUE(gTestSocket->send("vice:args\0"sv));
    EXPECT_TRUE(qemuDeviceCreated);
    EXPECT_FALSE(standardDeviceCreated);
}

TEST_F(ConnectorRegistryTest, RegisterHalDevice) {
    using namespace std::literals;

    auto device = std::make_shared<TestHalDevice>();
    auto connected_future = device->connected();
    auto closed_future = device->closed();
    bool factoryCalled = false;
    std::string factoryArguments;

    registry.registerHalDevice("TestHalDevice", mClientLoop.get(), mQemuLoop.get(),
                               [&](std::string_view args) {
                                   factoryCalled = true;
                                   factoryArguments = std::string(args);
                                   return device;
                               });

    // Use the ListenFn overload to directly get the created device factory.
    registry.listen([&](HostPortListener listener) {
        // This simulates the guest connecting and the vsock layer creating a
        // generic Connector plug.
        gTestSocket = new TestSocket();
        auto connectorPlug = std::get<PlugPtr>(listener(SocketPtr(gTestSocket)));
        gTestSocket->plug = std::move(connectorPlug);
        return true;
    });

    // Now, simulate the guest sending the pipe connection string. This will
    // cause the Connector plug to invoke our wrapperFactory.
    EXPECT_TRUE(gTestSocket->send("pipe:TestHalDevice:factoryArguments\0"sv));

    // Verify that our user-provided factory was called.
    EXPECT_TRUE(factoryCalled);
    EXPECT_EQ(factoryArguments, "factoryArguments");

    // Verify the rest of the connection flow.
    connected_future.wait_for(100ms);
    EXPECT_TRUE(connected_future.get());

    // Now we unplug, lest we get into weird states where
    // cleanup happens at the wrong time
    gTestSocket->plug->OnUnplug();
    closed_future.wait_for(100ms);
    EXPECT_TRUE(closed_future.get());

    // Cleanly exit loops before we destroy sockets etc.
    mClientLoop->ShutdownAndWait(100ms);
    mQemuLoop->ShutdownAndWait(100ms);
}

TEST_F(ConnectorRegistryTest, RegisterHalQemuDevice) {
    using namespace std::literals;

    auto device = std::make_shared<TestHalDevice>();
    auto connected_future = device->connected();
    auto closed_future = device->closed();
    bool factoryCalled = false;

    registry.registerHalQemuDevice("TestHalDevice", mClientLoop.get(), mQemuLoop.get(),
                                   [&](std::string_view /*args*/) {
                                       factoryCalled = true;
                                       return device;
                                   });

    registry.listen([&](HostPortListener listener) {
        gTestSocket = new TestSocket();
        auto connectorPlug = std::get<PlugPtr>(listener(SocketPtr(gTestSocket)));
        gTestSocket->plug = std::move(connectorPlug);
        return true;
    });

    EXPECT_TRUE(gTestSocket->send("pipe:qemud:TestHalDevice:args\0"sv));

    EXPECT_TRUE(factoryCalled);

    connected_future.wait_for(100ms);
    EXPECT_TRUE(connected_future.get());

    // Now we unplug, lest we get into weird states where
    // cleanup happens at the wrong time
    gTestSocket->plug->OnUnplug();
    closed_future.wait_for(100ms);
    EXPECT_TRUE(closed_future.get());

    // Cleanly exit loops before we destroy sockets etc.
    mClientLoop->ShutdownAndWait(100ms);
    mQemuLoop->ShutdownAndWait(100ms);
}

}  // namespace devices
}  // namespace goldfish
