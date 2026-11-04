
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

#include "goldfish/archive/collections/string.h"
#include "goldfish/archive/deque_archive.h"
#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/cable/saveload.h"
#include "goldfish/devices/connector_registry_impl.h"
#include "goldfish/devices/hal_plug_snapshot.h"
#include "goldfish/devices/hal_plug_to_i_plug_adapter.h"
#include "goldfish/devices/test/fake_vsock.h"

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

    static constexpr std::string_view kServiceName = "TestDevice"sv;

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
    static constexpr std::string_view kServiceName = "TestHalDevice"sv;

  private:
    std::promise<bool> mConnectedPromise;
    std::promise<bool> mClosedPromise;
};
}  // namespace

static bool gListenCalled = false;
static TestSocket* gTestSocket;

// Custom vsock::Listen implementation for unit tests.
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
        vsock::SetFakeListenFn(fake_vsock_listen);
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
    ASSERT_TRUE(registry.Listen(1234));
    ASSERT_TRUE(gListenCalled);
}

TEST_F(ConnectorRegistryTest, ListenOnlyOnce) {
    ASSERT_TRUE(registry.Listen(1234));
    ASSERT_FALSE(registry.Listen(1234));
}

TEST_F(ConnectorRegistryTest, ListenFnSuccess) {
    ASSERT_TRUE(
            registry.Listen([this](HostPortListener listener) { return mockListenFn(listener); }));
    ASSERT_TRUE(listenCalled);
}

TEST_F(ConnectorRegistryTest, RegisterQemuDeviceBeforeListen) {
    ASSERT_TRUE(registry.RegisterQemuDevice("device1", [](auto, auto, auto) { return nullptr; }));
}

TEST_F(ConnectorRegistryTest, RegisterDeviceBeforeListen) {
    ASSERT_TRUE(registry.RegisterDevice("device2", [](auto, auto, auto) { return nullptr; }));
}

TEST_F(ConnectorRegistryTest, RegisterQemuDeviceAfterListen) {
    registry.Listen(1234);  // Call listen first
    ASSERT_FALSE(registry.RegisterQemuDevice("device3", [](auto, auto, auto) { return nullptr; }));
}

TEST_F(ConnectorRegistryTest, RegisterDeviceAfterListen) {
    registry.Listen(1234);  // Call listen first
    ASSERT_FALSE(registry.RegisterDevice("device4", [](auto, auto, auto) { return nullptr; }));
}

TEST_F(ConnectorRegistryTest, RegisteredDeviceIsAvailable) {
#ifdef _WIN32
    // TODO(whollins,b/449212254): Fix this.
    GTEST_SKIP() << "currently broken on Windows";
#endif
    using namespace std::literals;

    bool standardDeviceCreated = false;
    bool qemuDeviceCreated = false;
    registry.RegisterDevice(
            "TestDevice", [&](cable::SocketPtr socket, const std::shared_ptr<PingTopic>& ping_topic,
                              std::string_view args) {
                standardDeviceCreated = true;
                return std::make_shared<TestDevice>(std::move(socket), false, args);
            });
    registry.RegisterQemuDevice(
            "TestDevice", [&](cable::SocketPtr socket, const std::shared_ptr<PingTopic>& ping_topic,
                              std::string_view args) {
                qemuDeviceCreated = true;
                return std::make_shared<TestDevice>(std::move(socket), false, args);
            });
    registry.Listen(1234);  // Call listen first
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
    registry.RegisterDevice(
            "TestDevice", [&](cable::SocketPtr socket, const std::shared_ptr<PingTopic>& ping_topic,
                              std::string_view args) {
                standardDeviceCreated = true;
                return std::make_shared<TestDevice>(std::move(socket), false, args);
            });
    registry.RegisterQemuDevice(
            "TestDevice", [&](cable::SocketPtr socket, const std::shared_ptr<PingTopic>& ping_topic,
                              std::string_view args) {
                qemuDeviceCreated = true;
                return std::make_shared<TestDevice>(std::move(socket), false, args);
            });
    registry.Listen(1234);  // Call listen first
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

    registry.RegisterHalDevice("TestHalDevice", mClientLoop.get(), mQemuLoop.get(),
                               [&](std::string_view args) {
                                   factoryCalled = true;
                                   factoryArguments = std::string(args);
                                   return device;
                               });

    // Use the ListenFn overload to directly get the created device factory.
    registry.Listen([&](HostPortListener listener) {
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
    mClientLoop->ShutdownAndWait(absl::Milliseconds(100)).IgnoreError();
    mQemuLoop->ShutdownAndWait(absl::Milliseconds(100)).IgnoreError();
}

TEST_F(ConnectorRegistryTest, RegisterHalQemuDevice) {
    using namespace std::literals;

    auto device = std::make_shared<TestHalDevice>();
    auto connected_future = device->connected();
    auto closed_future = device->closed();
    bool factoryCalled = false;

    registry.RegisterHalQemuDevice("TestHalDevice", mClientLoop.get(), mQemuLoop.get(),
                                   [&](std::string_view /*args*/) {
                                       factoryCalled = true;
                                       return device;
                                   });

    registry.Listen([&](HostPortListener listener) {
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
    mClientLoop->ShutdownAndWait(absl::Milliseconds(100)).IgnoreError();
    mQemuLoop->ShutdownAndWait(absl::Milliseconds(100)).IgnoreError();
}

namespace {
struct SnapshottableTestHalDevice : public TestHalDevice {
    bool SupportsSnapshot() const override { return true; }
    bool SaveToSnapshot(archive::IWriter& writer) const override {
        writer << std::string_view("state");
        return true;
    }
    bool LoadFromSnapshot(archive::IReader& reader) override {
        std::string state;
        return ReadValue(reader, state).ok();
    }
};
}  // namespace

// RegisterHalQemuDevice must (a) give the wrapped plug a snapshot identity so
// vsock saves it instead of RST'ing it, and (b) publish the factory under the
// same key ("q<name>") so the loader can find it again in a fresh process.
TEST_F(ConnectorRegistryTest, RegisterHalQemuDeviceMakesOptedInPlugSnapshottable) {
    using namespace std::literals;

    auto device = std::make_shared<SnapshottableTestHalDevice>();
    auto connected_future = device->connected();
    auto closed_future = device->closed();
    int factory_calls = 0;

    {
        ConnectorRegistry scoped_registry;
        scoped_registry.RegisterHalQemuDevice("SnapshotHalDevice", mClientLoop.get(),
                                              mQemuLoop.get(), [&](std::string_view /*args*/) {
                                                  ++factory_calls;
                                                  return device;
                                              });
        auto entry = HalPlugSnapshotRegistry::Get().Find("qSnapshotHalDevice");
        ASSERT_TRUE(entry.has_value());
        EXPECT_EQ(entry->client_loop, mClientLoop.get());
        EXPECT_EQ(entry->qemu_loop, mQemuLoop.get());
        EXPECT_EQ(entry->factory("x"), device);
        EXPECT_EQ(factory_calls, 1);
        EXPECT_FALSE(HalPlugSnapshotRegistry::Get().Find("-SnapshotHalDevice").has_value())
                << "qemud and plain devices live under different keys";

        scoped_registry.Listen([&](HostPortListener listener) {
            gTestSocket = new TestSocket();
            gTestSocket->plug = std::get<PlugPtr>(listener(SocketPtr(gTestSocket)));
            return true;
        });
        EXPECT_TRUE(gTestSocket->send("pipe:qemud:SnapshotHalDevice:a=b\0"sv));
        EXPECT_EQ(factory_calls, 2);
        connected_future.wait_for(100ms);
        EXPECT_TRUE(connected_future.get());

        const IPlug& plug = *gTestSocket->plug;
        EXPECT_TRUE(plug.SupportsLoadingFromSnapshot());
        EXPECT_EQ(plug.GetSnapshotTypeId(), HalPlugToIPlugAdapter::kSnapshotTypeId);
        const auto* adapter = dynamic_cast<const HalPlugToIPlugAdapter*>(&plug);
        ASSERT_NE(adapter, nullptr);
        ASSERT_TRUE(adapter->GetSnapshotIdentity().has_value());
        EXPECT_EQ(adapter->GetSnapshotIdentity()->device, "qSnapshotHalDevice");
        EXPECT_EQ(adapter->GetSnapshotIdentity()->args, "a=b");

        gTestSocket->plug->OnUnplug();
        closed_future.wait_for(100ms);
        EXPECT_TRUE(closed_future.get());
    }
    // A destroyed registry takes its snapshot factories with it.
    EXPECT_FALSE(HalPlugSnapshotRegistry::Get().Find("qSnapshotHalDevice").has_value());

    mClientLoop->ShutdownAndWait(absl::Milliseconds(100)).IgnoreError();
    mQemuLoop->ShutdownAndWait(absl::Milliseconds(100)).IgnoreError();
}

// A guest connection that is still in the Connector stage (it has not sent
// its full "pipe:..." name yet) must survive a snapshot: Listen() installs the
// Connector loader, and the restored Connector resumes over this registry's
// device table.
TEST_F(ConnectorRegistryTest, ConnectorStageStreamIsRestoredFromSnapshot) {
    using namespace std::literals;

    std::string created_args;
    registry.RegisterQemuDevice(
            "TestDevice", [&](cable::SocketPtr socket, const std::shared_ptr<PingTopic>& ping_topic,
                              std::string_view args) {
                created_args = std::string(args);
                return std::make_shared<TestDevice>(std::move(socket), true, args);
            });
    ASSERT_TRUE(registry.Listen(1234));

    archive::DequeArchive archive;
    EXPECT_TRUE(gTestSocket->send("pipe:qemud:Test"sv));
    EXPECT_TRUE(cable::SavePlugToSnapshot(*gTestSocket->plug, archive));
    gTestSocket->plug->OnUnplug();

    // The "guest" reconnects after the load with a fresh socket.
    TestSocket restored_socket;
    cable::PlugOrSocket loaded = cable::LoadPlugFromSnapshot(SocketPtr(&restored_socket), archive);
    ASSERT_TRUE(std::holds_alternative<PlugPtr>(loaded));
    restored_socket.plug = std::get<PlugPtr>(std::move(loaded));
    EXPECT_TRUE(archive.Empty());

    EXPECT_TRUE(restored_socket.send("Device:x=1\0rest"sv));
    EXPECT_EQ(created_args, "x=1");
    auto* device = dynamic_cast<TestDevice*>(restored_socket.plug.get());
    ASSERT_NE(device, nullptr);
    EXPECT_EQ(device->mData, "rest");
    restored_socket.plug->OnUnplug();
}

// The emulator runs two registries at once (HAL devices on vsock:5000, test
// tools on vsock:5002). A Connector saved on one must be restored over *that*
// registry's device table, not over whichever registry listened last.
TEST_F(ConnectorRegistryTest, ConnectorIsRestoredIntoItsOwnRegistry) {
    using namespace std::literals;

    bool hal_created = false;
    registry.RegisterQemuDevice(
            "TestDevice", [&](cable::SocketPtr socket, const std::shared_ptr<PingTopic>& ping_topic,
                              std::string_view args) {
                hal_created = true;
                return std::make_shared<TestDevice>(std::move(socket), true, args);
            });
    ASSERT_TRUE(registry.Listen(5000));
    TestSocket* hal_socket = gTestSocket;

    bool tools_created = false;
    ConnectorRegistry tools_registry;
    tools_registry.RegisterDevice("OtherDevice", [&](cable::SocketPtr socket,
                                                     const std::shared_ptr<PingTopic>& ping_topic,
                                                     std::string_view args) {
        tools_created = true;
        return std::make_shared<TestDevice>(std::move(socket), false, args);
    });
    TestSocket* tools_socket = nullptr;
    ASSERT_TRUE(tools_registry.Listen([&](HostPortListener listener) {
        tools_socket = new TestSocket();
        tools_socket->plug = std::get<PlugPtr>(listener(SocketPtr(tools_socket)));
        return true;
    }));
    // tools_registry listened last; the HAL Connector must still find `registry`.

    archive::DequeArchive archive;
    EXPECT_TRUE(hal_socket->send("pipe:qemud:Test"sv));
    EXPECT_TRUE(cable::SavePlugToSnapshot(*hal_socket->plug, archive));
    hal_socket->plug->OnUnplug();

    TestSocket restored_socket;
    cable::PlugOrSocket loaded = cable::LoadPlugFromSnapshot(SocketPtr(&restored_socket), archive);
    ASSERT_TRUE(std::holds_alternative<PlugPtr>(loaded));
    restored_socket.plug = std::get<PlugPtr>(std::move(loaded));
    EXPECT_TRUE(restored_socket.send("Device\0"sv));
    EXPECT_TRUE(hal_created);
    EXPECT_FALSE(tools_created);
    restored_socket.plug->OnUnplug();

    tools_socket->plug->OnUnplug();
    delete tools_socket;
}

// With no registry listening under the saved id the loader must hand the
// socket back instead of crashing.
TEST_F(ConnectorRegistryTest, ConnectorLoaderWithoutListeningRegistryReturnsSocket) {
    using namespace std::literals;

    archive::DequeArchive archive;
    {
        ConnectorRegistry scoped_registry;
        scoped_registry.Listen([&](HostPortListener listener) {
            gTestSocket = new TestSocket();
            gTestSocket->plug = std::get<PlugPtr>(listener(SocketPtr(gTestSocket)));
            return true;
        });
        EXPECT_TRUE(gTestSocket->send("pipe:qemud:Te"sv));
        EXPECT_TRUE(cable::SavePlugToSnapshot(*gTestSocket->plug, archive));
        gTestSocket->plug->OnUnplug();
    }
    // `scoped_registry` was the listening registry and is gone now.

    TestSocket restored_socket;
    cable::PlugOrSocket loaded = cable::LoadPlugFromSnapshot(SocketPtr(&restored_socket), archive);
    ASSERT_TRUE(std::holds_alternative<SocketPtr>(loaded));
    EXPECT_EQ(std::get<SocketPtr>(loaded).get(), &restored_socket);
}

// Destroying a registry that never listened must not withdraw the id of the
// one that did.
TEST_F(ConnectorRegistryTest, DestroyingNonListeningRegistryKeepsConnectorLoader) {
    using namespace std::literals;

    bool created = false;
    registry.RegisterDevice(
            "TestDevice", [&](cable::SocketPtr socket, const std::shared_ptr<PingTopic>& ping_topic,
                              std::string_view args) {
                created = true;
                return std::make_shared<TestDevice>(std::move(socket), false, args);
            });
    ASSERT_TRUE(registry.Listen(1234));

    archive::DequeArchive archive;
    EXPECT_TRUE(gTestSocket->send("pipe:Test"sv));
    EXPECT_TRUE(cable::SavePlugToSnapshot(*gTestSocket->plug, archive));
    gTestSocket->plug->OnUnplug();

    {
        ConnectorRegistry never_listened;
    }

    TestSocket restored_socket;
    cable::PlugOrSocket loaded = cable::LoadPlugFromSnapshot(SocketPtr(&restored_socket), archive);
    ASSERT_TRUE(std::holds_alternative<PlugPtr>(loaded));
    restored_socket.plug = std::get<PlugPtr>(std::move(loaded));
    EXPECT_TRUE(restored_socket.send("Device\0"sv));
    EXPECT_TRUE(created);
    restored_socket.plug->OnUnplug();
}

}  // namespace devices
}  // namespace goldfish
