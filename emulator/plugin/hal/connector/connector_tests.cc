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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status_matchers.h"

#include "goldfish/archive/collections/string.h"
#include "goldfish/archive/deque_archive.h"
#include "goldfish/devices/cable/saveload.h"
#include "goldfish/devices/connector.h"

namespace goldfish {
using archive::DequeArchive;

namespace devices {
using ::absl_testing::IsOkAndHolds;

using cable::IPlug;
using cable::PlugPtr;
using cable::SocketPtr;

namespace {
struct TestSocket : public cable::ISocket {
    void SendAsync(const void* data, size_t size) override {}

    PlugPtr SwitchPlug(PlugPtr newPlug) override {
        plug.swap(newPlug);
        return newPlug;
    }

    PlugPtr UnplugImpl() override { return std::move(plug); }

    bool send(const std::string_view data) { return plug->OnReceive(data.data(), data.size()); }

    PlugPtr plug;
};

struct TestDevice : public cable::IPlug {
    TestDevice(cable::SocketPtr socket, bool isQemud, std::string_view args)
            : mSocket(std::move(socket))
            , mIsQemud(isQemud)
            , mArgs(std::string(args.begin(), args.end())) {}

    cable::SocketPtr OnUnplug() override { return std::move(mSocket); }

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

    cable::SocketPtr mSocket;
    const bool mIsQemud;
    std::string mArgs;
    std::string mData;
};

const DeviceEntry kDeviceEntries[] = {
    {"-TestDevice",
     [](cable::SocketPtr socket, const std::shared_ptr<PingTopic>& ping_topic,
        std::string_view args) {
         return std::make_shared<TestDevice>(std::move(socket), false, args);
     }},
    {"qTestDevice",
     [](cable::SocketPtr socket, const std::shared_ptr<PingTopic>& ping_topic,
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
        std::shared_ptr<PingTopic> ping_topic = PingTopic::Create();
        TestSocket testSocket;
        testSocket.plug = std::make_shared<Connector>(SocketPtr(&testSocket), ping_topic,
                                                      kDeviceEntries, kDeviceEntriesSize);
        EXPECT_TRUE(testSocket.send("incom"sv));
        EXPECT_TRUE(testSocket.send("plete"sv));
        EXPECT_TRUE(SavePlugToSnapshot(*testSocket.plug, archive));
        testSocket.plug->OnUnplug();
    }

    EXPECT_THAT(ReadOneValue<std::string>(archive), IsOkAndHolds("Connector"));
    EXPECT_THAT(ReadOneValue<std::string>(archive), IsOkAndHolds("")) << "registry id";
    EXPECT_THAT(ReadOneValue<std::string>(archive), IsOkAndHolds("incomplete"));
}

TEST(Connector, oversized_partial_request_is_an_error) {
    using namespace std::literals;

    DequeArchive archive;
    std::shared_ptr<PingTopic> ping_topic = PingTopic::Create();
    TestSocket testSocket;
    testSocket.plug = std::make_shared<Connector>(SocketPtr(&testSocket), ping_topic,
                                                  kDeviceEntries, kDeviceEntriesSize);
    const std::string max_chunk(Connector::kMaxSnapshotBufferSize, 'a');
    EXPECT_TRUE(testSocket.send(max_chunk));
    EXPECT_FALSE(testSocket.send("b"sv));
    EXPECT_FALSE(SavePlugToSnapshot(*testSocket.plug, archive));
    testSocket.plug->OnUnplug();
    EXPECT_TRUE(archive.Empty());
}

TEST(Connector, bad_request) {
    using namespace std::literals;

    DequeArchive archive;

    std::shared_ptr<PingTopic> ping_topic = PingTopic::Create();
    TestSocket testSocket;
    testSocket.plug = std::make_shared<Connector>(SocketPtr(&testSocket), ping_topic,
                                                  kDeviceEntries, kDeviceEntriesSize);
    EXPECT_FALSE(testSocket.send("bad\0"sv));
    EXPECT_FALSE(testSocket.send("pipe:TestDevice:correct but ignored\0"sv));
    EXPECT_FALSE(SavePlugToSnapshot(*testSocket.plug, archive));
    testSocket.plug->OnUnplug();

    EXPECT_TRUE(archive.Empty());
}

TEST(Connector, unknown_device) {
    using namespace std::literals;

    DequeArchive archive;

    std::shared_ptr<PingTopic> ping_topic = PingTopic::Create();
    TestSocket testSocket;
    testSocket.plug = std::make_shared<Connector>(SocketPtr(&testSocket), ping_topic,
                                                  kDeviceEntries, kDeviceEntriesSize);
    EXPECT_FALSE(testSocket.send("pipe:unknown:args\0"sv));
    EXPECT_FALSE(testSocket.send("more data"sv));
    EXPECT_FALSE(SavePlugToSnapshot(*testSocket.plug, archive));
    testSocket.plug->OnUnplug();

    EXPECT_TRUE(archive.Empty());
}

TEST(Connector, unknown_qemud_device) {
    using namespace std::literals;

    DequeArchive archive;

    std::shared_ptr<PingTopic> ping_topic = PingTopic::Create();
    TestSocket testSocket;
    testSocket.plug = std::make_shared<Connector>(SocketPtr(&testSocket), ping_topic,
                                                  kDeviceEntries, kDeviceEntriesSize);
    EXPECT_FALSE(testSocket.send("pipe:qemud:unknown:args\0"sv));
    EXPECT_FALSE(testSocket.send("more data"sv));
    EXPECT_FALSE(SavePlugToSnapshot(*testSocket.plug, archive));
    testSocket.plug->OnUnplug();

    EXPECT_TRUE(archive.Empty());
}

TEST(Connector, qemud_TestDevice_args_unconsumed) {
#ifdef _WIN32
    // TODO(whollins,b/449212254): Fix this.
    GTEST_SKIP() << "currently broken on Windows";
#endif
    using namespace std::literals;

    DequeArchive archive;

    {
        std::shared_ptr<PingTopic> ping_topic = PingTopic::Create();
        TestSocket testSocket;
        testSocket.plug = std::make_shared<Connector>(SocketPtr(&testSocket), ping_topic,
                                                      kDeviceEntries, kDeviceEntriesSize);
        EXPECT_TRUE(testSocket.send("pipe:qemud:TestDe"sv));
        EXPECT_TRUE(testSocket.send("vice:args\0unconsumed"sv));
        EXPECT_TRUE(SavePlugToSnapshot(*testSocket.plug, archive));
        testSocket.plug->OnUnplug();
    }

    EXPECT_THAT(ReadOneValue<std::string>(archive), IsOkAndHolds("TestDevice"));
    EXPECT_THAT(ReadOneValue<bool>(archive), IsOkAndHolds(true));
    EXPECT_THAT(ReadOneValue<std::string>(archive), IsOkAndHolds("args"));
    EXPECT_THAT(ReadOneValue<std::string>(archive), IsOkAndHolds("unconsumed"));
}

TEST(Connector, qemud_TestDevice_unconsumed) {
#ifdef _WIN32
    // TODO(whollins,b/449212254): Fix this.
    GTEST_SKIP() << "currently broken on Windows";
#endif
    using namespace std::literals;

    DequeArchive archive;

    {
        std::shared_ptr<PingTopic> ping_topic = PingTopic::Create();
        TestSocket testSocket;
        testSocket.plug = std::make_shared<Connector>(SocketPtr(&testSocket), ping_topic,
                                                      kDeviceEntries, kDeviceEntriesSize);
        EXPECT_TRUE(testSocket.send("pipe:qemud:TestDe"sv));
        EXPECT_TRUE(testSocket.send("vice\0unconsumed"sv));
        EXPECT_TRUE(SavePlugToSnapshot(*testSocket.plug, archive));
        testSocket.plug->OnUnplug();
    }

    EXPECT_THAT(ReadOneValue<std::string>(archive), IsOkAndHolds("TestDevice"));
    EXPECT_THAT(ReadOneValue<bool>(archive), IsOkAndHolds(true));
    EXPECT_THAT(ReadOneValue<std::string>(archive), IsOkAndHolds(""));
    EXPECT_THAT(ReadOneValue<std::string>(archive), IsOkAndHolds("unconsumed"));
}

TEST(Connector, TestDevice_args_unconsumed) {
#ifdef _WIN32
    // TODO(whollins,b/449212254): Fix this.
    GTEST_SKIP() << "currently broken on Windows";
#endif
    using namespace std::literals;

    DequeArchive archive;

    {
        std::shared_ptr<PingTopic> ping_topic = PingTopic::Create();
        TestSocket testSocket;
        testSocket.plug = std::make_shared<Connector>(SocketPtr(&testSocket), ping_topic,
                                                      kDeviceEntries, kDeviceEntriesSize);
        EXPECT_TRUE(testSocket.send("pipe:TestDe"sv));
        EXPECT_TRUE(testSocket.send("vice:args\0unconsumed"sv));
        EXPECT_TRUE(SavePlugToSnapshot(*testSocket.plug, archive));
        testSocket.plug->OnUnplug();
    }

    EXPECT_THAT(ReadOneValue<std::string>(archive), IsOkAndHolds("TestDevice"));
    EXPECT_THAT(ReadOneValue<bool>(archive), IsOkAndHolds(false));
    EXPECT_THAT(ReadOneValue<std::string>(archive), IsOkAndHolds("args"));
    EXPECT_THAT(ReadOneValue<std::string>(archive), IsOkAndHolds("unconsumed"));
}

TEST(Connector, TestDevice_unconsumed) {
#ifdef _WIN32
    // TODO(whollins,b/449212254): Fix this.
    GTEST_SKIP() << "currently broken on Windows";
#endif
    using namespace std::literals;

    DequeArchive archive;

    {
        std::shared_ptr<PingTopic> ping_topic = PingTopic::Create();
        TestSocket testSocket;
        testSocket.plug = std::make_shared<Connector>(SocketPtr(&testSocket), ping_topic,
                                                      kDeviceEntries, kDeviceEntriesSize);
        EXPECT_TRUE(testSocket.send("pipe:TestDe"sv));
        EXPECT_TRUE(testSocket.send("vice\0unconsumed"sv));
        EXPECT_TRUE(SavePlugToSnapshot(*testSocket.plug, archive));
        testSocket.plug->OnUnplug();
    }

    EXPECT_THAT(ReadOneValue<std::string>(archive), IsOkAndHolds("TestDevice"));
    EXPECT_THAT(ReadOneValue<bool>(archive), IsOkAndHolds(false));
    EXPECT_THAT(ReadOneValue<std::string>(archive), IsOkAndHolds(""));
    EXPECT_THAT(ReadOneValue<std::string>(archive), IsOkAndHolds("unconsumed"));
}

namespace {
constexpr std::string_view kRegistryId = "vsock:5000";

// Saves a Connector (registry id `kRegistryId`) that has received `partial`
// (no terminator yet) and returns the archive positioned right after the type
// id, i.e. where `cable::LoadPlugFromSnapshot` hands over to the loader.
DequeArchive SavePartialConnector(const std::string_view partial) {
    DequeArchive archive;
    std::shared_ptr<PingTopic> ping_topic = PingTopic::Create();
    TestSocket testSocket;
    testSocket.plug =
            std::make_shared<Connector>(SocketPtr(&testSocket), ping_topic, kDeviceEntries,
                                        kDeviceEntriesSize, std::string(kRegistryId));
    EXPECT_TRUE(testSocket.send(partial));
    EXPECT_TRUE(SavePlugToSnapshot(*testSocket.plug, archive));
    testSocket.plug->OnUnplug();
    EXPECT_THAT(ReadOneValue<std::string>(archive), IsOkAndHolds(Connector::kSnapshotTypeId));
    return archive;
}

// Resolver that knows `kRegistryId` only and records what it was asked for.
struct TestResolver {
    std::optional<Connector::Target> operator()(const std::string_view id) {
        asked.emplace_back(id);
        if (id != kRegistryId) {
            return std::nullopt;
        }
        return Connector::Target{.ping_topic = ping_topic,
                                 .devices_entries = kDeviceEntries,
                                 .devices_entries_size = kDeviceEntriesSize};
    }
    std::shared_ptr<PingTopic> ping_topic = PingTopic::Create();
    std::vector<std::string> asked;
};
}  // namespace

TEST(Connector, load_resumes_partial_qemud_request) {
    using namespace std::literals;

    DequeArchive archive = SavePartialConnector("pipe:qemud:TestDev"sv);

    TestResolver resolver;
    TestSocket testSocket;
    cable::PlugOrSocket loaded =
            Connector::LoadFromSnapshot(SocketPtr(&testSocket), archive, std::ref(resolver));
    ASSERT_TRUE(std::holds_alternative<PlugPtr>(loaded));
    testSocket.plug = std::get<PlugPtr>(std::move(loaded));
    EXPECT_TRUE(archive.Empty());
    EXPECT_THAT(resolver.asked, ::testing::ElementsAre(kRegistryId));

    // The rest of the request arrives after the load: the restored prefix and
    // the new bytes together must select the device, pass the args and
    // forward the unconsumed tail.
    EXPECT_TRUE(testSocket.send("ice:a=b\0tail"sv));

    auto* device = dynamic_cast<TestDevice*>(testSocket.plug.get());
    ASSERT_NE(device, nullptr);
    EXPECT_TRUE(device->mIsQemud);
    EXPECT_EQ(device->mArgs, "a=b");
    EXPECT_EQ(device->mData, "tail");
    testSocket.plug->OnUnplug();
}

TEST(Connector, load_with_empty_buffer_behaves_like_new) {
    using namespace std::literals;

    DequeArchive archive = SavePartialConnector(""sv);

    TestResolver resolver;
    TestSocket testSocket;
    cable::PlugOrSocket loaded =
            Connector::LoadFromSnapshot(SocketPtr(&testSocket), archive, std::ref(resolver));
    ASSERT_TRUE(std::holds_alternative<PlugPtr>(loaded));
    testSocket.plug = std::get<PlugPtr>(std::move(loaded));

    EXPECT_TRUE(testSocket.send("pipe:TestDevice\0"sv));
    auto* device = dynamic_cast<TestDevice*>(testSocket.plug.get());
    ASSERT_NE(device, nullptr);
    EXPECT_FALSE(device->mIsQemud);
    testSocket.plug->OnUnplug();
}

TEST(Connector, load_round_trips_again_after_load) {
    using namespace std::literals;

    DequeArchive archive = SavePartialConnector("pipe:Te"sv);

    TestResolver resolver;
    TestSocket testSocket;
    cable::PlugOrSocket loaded =
            Connector::LoadFromSnapshot(SocketPtr(&testSocket), archive, std::ref(resolver));
    ASSERT_TRUE(std::holds_alternative<PlugPtr>(loaded));
    testSocket.plug = std::get<PlugPtr>(std::move(loaded));
    EXPECT_TRUE(testSocket.send("stDev"sv));

    // The registry id travels along, so a second save/load finds the same
    // registry again.
    DequeArchive again;
    EXPECT_TRUE(SavePlugToSnapshot(*testSocket.plug, again));
    EXPECT_THAT(ReadOneValue<std::string>(again), IsOkAndHolds(Connector::kSnapshotTypeId));
    EXPECT_THAT(ReadOneValue<std::string>(again), IsOkAndHolds(kRegistryId));
    EXPECT_THAT(ReadOneValue<std::string>(again), IsOkAndHolds("pipe:TestDev"));
    testSocket.plug->OnUnplug();
}

TEST(Connector, load_rejects_unknown_registry_id) {
    using namespace std::literals;

    DequeArchive archive;
    archive << "vsock:5002"sv << std::string("pipe:Te");

    TestResolver resolver;
    TestSocket testSocket;
    cable::PlugOrSocket loaded =
            Connector::LoadFromSnapshot(SocketPtr(&testSocket), archive, std::ref(resolver));
    EXPECT_TRUE(std::holds_alternative<SocketPtr>(loaded));
    EXPECT_EQ(std::get<SocketPtr>(loaded).get(), &testSocket);
    EXPECT_THAT(resolver.asked, ::testing::ElementsAre("vsock:5002"));
}

TEST(Connector, load_rejects_truncated_record) {
    DequeArchive archive;
    archive << kRegistryId << size_t{10};  // claims 10 bytes, provides none

    TestResolver resolver;
    TestSocket testSocket;
    cable::PlugOrSocket loaded =
            Connector::LoadFromSnapshot(SocketPtr(&testSocket), archive, std::ref(resolver));
    EXPECT_TRUE(std::holds_alternative<SocketPtr>(loaded));
    EXPECT_EQ(std::get<SocketPtr>(loaded).get(), &testSocket);
    EXPECT_TRUE(resolver.asked.empty()) << "rejected before resolving";
}

TEST(Connector, load_rejects_oversized_record) {
    DequeArchive archive;
    archive << kRegistryId << (Connector::kMaxSnapshotBufferSize + 1);

    TestResolver resolver;
    TestSocket testSocket;
    cable::PlugOrSocket loaded =
            Connector::LoadFromSnapshot(SocketPtr(&testSocket), archive, std::ref(resolver));
    EXPECT_TRUE(std::holds_alternative<SocketPtr>(loaded));
}

TEST(Connector, load_rejects_buffer_with_terminator) {
    using namespace std::literals;

    DequeArchive archive;
    archive << kRegistryId << std::string("pipe:TestDevice\0junk"sv);

    TestResolver resolver;
    TestSocket testSocket;
    cable::PlugOrSocket loaded =
            Connector::LoadFromSnapshot(SocketPtr(&testSocket), archive, std::ref(resolver));
    EXPECT_TRUE(std::holds_alternative<SocketPtr>(loaded));
}

}  // namespace devices
}  // namespace goldfish
