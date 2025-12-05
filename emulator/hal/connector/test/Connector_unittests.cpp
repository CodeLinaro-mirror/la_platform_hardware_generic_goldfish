// Copyright 2024 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include <gtest/gtest.h>

#include "goldfish/archive/DequeArchive.h"
#include "goldfish/devices/Connector.h"
#include "goldfish/devices/cable/saveload.h"

namespace goldfish {
using archive::DequeArchive;

namespace devices {
using cable::IPlug;
using cable::PlugPtr;
using cable::SocketPtr;

namespace {
struct TestSocket : public cable::ISocket {
    void sendAsync(const void* data, size_t size) override {}

    PlugPtr switchPlug(PlugPtr newPlug) override {
        plug.swap(newPlug);
        return newPlug;
    }

    PlugPtr unplugImpl() override { return std::move(plug); }

    bool send(const std::string_view data) { return plug->onReceive(data.data(), data.size()); }

    PlugPtr plug;
};

struct TestDevice : public cable::IPlug {
    TestDevice(cable::SocketPtr socket, bool isQemud, std::string_view args)
            : mSocket(std::move(socket))
            , mIsQemud(isQemud)
            , mArgs(std::string(args.begin(), args.end())) {}

    cable::SocketPtr onUnplug() override { return std::move(mSocket); }

    bool onReceive(const void* data, size_t size) override {
        mData.append(static_cast<const char*>(data), size);
        return true;
    }

    bool supportsLoadingFromSnapshot() const override { return true; }

    TypeId getSnapshotTypeId() const override {
        using namespace std::string_literals;
        return "TestDevice"s;
    }

    bool saveStateToSnapshot(archive::IWriter& writer) const override {
        writer << mIsQemud << mArgs << mData;
        return true;
    }

    cable::SocketPtr mSocket;
    const bool mIsQemud;
    std::string mArgs;
    std::string mData;
};

const DeviceEntry kDeviceEntries[] = {
    {"-TestDevice",
     [](cable::SocketPtr socket, const std::shared_ptr<PingTopic>& pingTopic,
        std::string_view args) {
         return std::make_shared<TestDevice>(std::move(socket), false, args);
     }},
    {"qTestDevice",
     [](cable::SocketPtr socket, const std::shared_ptr<PingTopic>& pingTopic,
        std::string_view args) {
         return std::make_shared<TestDevice>(std::move(socket), true, args);
     }},
};

constexpr size_t kDeviceEntriesSize = sizeof(kDeviceEntries) / sizeof(kDeviceEntries[0]);
}  // namespace

TEST(Connector, incomplete_request) {
    using namespace std::literals;

    DequeArchive archive;

    {
        std::shared_ptr<PingTopic> pingTopic = std::make_shared<PingTopic>();
        TestSocket testSocket;
        testSocket.plug = std::make_shared<Connector>(SocketPtr(&testSocket), pingTopic,
                                                      kDeviceEntries, kDeviceEntriesSize);
        EXPECT_TRUE(testSocket.send("incom"sv));
        EXPECT_TRUE(testSocket.send("plete"sv));
        EXPECT_TRUE(savePlugToSnapshot(*testSocket.plug, archive));
        testSocket.plug->onUnplug();
    }

    EXPECT_EQ(getString(archive), "Connector");
    EXPECT_EQ(getString(archive), "incomplete");
}

TEST(Connector, bad_request) {
    using namespace std::literals;

    DequeArchive archive;

    std::shared_ptr<PingTopic> pingTopic = std::make_shared<PingTopic>();
    TestSocket testSocket;
    testSocket.plug = std::make_shared<Connector>(SocketPtr(&testSocket), pingTopic, kDeviceEntries,
                                                  kDeviceEntriesSize);
    EXPECT_FALSE(testSocket.send("bad\0"sv));
    EXPECT_FALSE(testSocket.send("pipe:TestDevice:correct but ignored\0"sv));
    EXPECT_FALSE(savePlugToSnapshot(*testSocket.plug, archive));
    testSocket.plug->onUnplug();

    EXPECT_TRUE(archive.empty());
}

TEST(Connector, unknown_device) {
    using namespace std::literals;

    DequeArchive archive;

    std::shared_ptr<PingTopic> pingTopic = std::make_shared<PingTopic>();
    TestSocket testSocket;
    testSocket.plug = std::make_shared<Connector>(SocketPtr(&testSocket), pingTopic, kDeviceEntries,
                                                  kDeviceEntriesSize);
    EXPECT_FALSE(testSocket.send("pipe:unknown:args\0"sv));
    EXPECT_FALSE(testSocket.send("more data"sv));
    EXPECT_FALSE(savePlugToSnapshot(*testSocket.plug, archive));
    testSocket.plug->onUnplug();

    EXPECT_TRUE(archive.empty());
}

TEST(Connector, unknown_qemud_device) {
    using namespace std::literals;

    DequeArchive archive;

    std::shared_ptr<PingTopic> pingTopic = std::make_shared<PingTopic>();
    TestSocket testSocket;
    testSocket.plug = std::make_shared<Connector>(SocketPtr(&testSocket), pingTopic, kDeviceEntries,
                                                  kDeviceEntriesSize);
    EXPECT_FALSE(testSocket.send("pipe:qemud:unknown:args\0"sv));
    EXPECT_FALSE(testSocket.send("more data"sv));
    EXPECT_FALSE(savePlugToSnapshot(*testSocket.plug, archive));
    testSocket.plug->onUnplug();

    EXPECT_TRUE(archive.empty());
}

TEST(Connector, qemud_TestDevice_args_unconsumed) {
#ifdef _WIN32
    // TODO(whollins,b/449212254): Fix this.
    GTEST_SKIP() << "currently broken on Windows";
#endif
    using namespace std::literals;

    DequeArchive archive;

    {
        std::shared_ptr<PingTopic> pingTopic = std::make_shared<PingTopic>();
        TestSocket testSocket;
        testSocket.plug = std::make_shared<Connector>(SocketPtr(&testSocket), pingTopic,
                                                      kDeviceEntries, kDeviceEntriesSize);
        EXPECT_TRUE(testSocket.send("pipe:qemud:TestDe"sv));
        EXPECT_TRUE(testSocket.send("vice:args\0unconsumed"sv));
        EXPECT_TRUE(savePlugToSnapshot(*testSocket.plug, archive));
        testSocket.plug->onUnplug();
    }

    EXPECT_EQ(getString(archive), "TestDevice");  // type
    EXPECT_EQ(getUnsigned(archive), 1);           // isQemud
    EXPECT_EQ(getString(archive), "args");        // args
    EXPECT_EQ(getString(archive), "unconsumed");
}

TEST(Connector, qemud_TestDevice_unconsumed) {
#ifdef _WIN32
    // TODO(whollins,b/449212254): Fix this.
    GTEST_SKIP() << "currently broken on Windows";
#endif
    using namespace std::literals;

    DequeArchive archive;

    {
        std::shared_ptr<PingTopic> pingTopic = std::make_shared<PingTopic>();
        TestSocket testSocket;
        testSocket.plug = std::make_shared<Connector>(SocketPtr(&testSocket), pingTopic,
                                                      kDeviceEntries, kDeviceEntriesSize);
        EXPECT_TRUE(testSocket.send("pipe:qemud:TestDe"sv));
        EXPECT_TRUE(testSocket.send("vice\0unconsumed"sv));
        EXPECT_TRUE(savePlugToSnapshot(*testSocket.plug, archive));
        testSocket.plug->onUnplug();
    }

    EXPECT_EQ(getString(archive), "TestDevice");  // type
    EXPECT_EQ(getUnsigned(archive), 1);           // isQemud
    EXPECT_EQ(getString(archive), "");            // args
    EXPECT_EQ(getString(archive), "unconsumed");
}

TEST(Connector, TestDevice_args_unconsumed) {
#ifdef _WIN32
    // TODO(whollins,b/449212254): Fix this.
    GTEST_SKIP() << "currently broken on Windows";
#endif
    using namespace std::literals;

    DequeArchive archive;

    {
        std::shared_ptr<PingTopic> pingTopic = std::make_shared<PingTopic>();
        TestSocket testSocket;
        testSocket.plug = std::make_shared<Connector>(SocketPtr(&testSocket), pingTopic,
                                                      kDeviceEntries, kDeviceEntriesSize);
        EXPECT_TRUE(testSocket.send("pipe:TestDe"sv));
        EXPECT_TRUE(testSocket.send("vice:args\0unconsumed"sv));
        EXPECT_TRUE(savePlugToSnapshot(*testSocket.plug, archive));
        testSocket.plug->onUnplug();
    }

    EXPECT_EQ(getString(archive), "TestDevice");  // type
    EXPECT_EQ(getUnsigned(archive), 0);           // isQemud
    EXPECT_EQ(getString(archive), "args");        // args
    EXPECT_EQ(getString(archive), "unconsumed");
}

TEST(Connector, TestDevice_unconsumed) {
#ifdef _WIN32
    // TODO(whollins,b/449212254): Fix this.
    GTEST_SKIP() << "currently broken on Windows";
#endif
    using namespace std::literals;

    DequeArchive archive;

    {
        std::shared_ptr<PingTopic> pingTopic = std::make_shared<PingTopic>();
        TestSocket testSocket;
        testSocket.plug = std::make_shared<Connector>(SocketPtr(&testSocket), pingTopic,
                                                      kDeviceEntries, kDeviceEntriesSize);
        EXPECT_TRUE(testSocket.send("pipe:TestDe"sv));
        EXPECT_TRUE(testSocket.send("vice\0unconsumed"sv));
        EXPECT_TRUE(savePlugToSnapshot(*testSocket.plug, archive));
        testSocket.plug->onUnplug();
    }

    EXPECT_EQ(getString(archive), "TestDevice");  // type
    EXPECT_EQ(getUnsigned(archive), 0);           // isQemud
    EXPECT_EQ(getString(archive), "");            // args
    EXPECT_EQ(getString(archive), "unconsumed");
}

}  // namespace devices
}  // namespace goldfish
