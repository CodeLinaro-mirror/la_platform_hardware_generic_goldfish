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

// Snapshot save/load of HalPlug based connections:
//   HalPlug (opt-in) <- HalPlugToIPlugAdapter <- HalPlugSnapshotRegistry/cable::PlugLoader.

#include "goldfish/devices/hal_plug_snapshot.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status_matchers.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

#include "goldfish/archive/collections/string.h"
#include "goldfish/archive/deque_archive.h"
#include "goldfish/archive/reader.h"
#include "goldfish/archive/writer.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/cable/saveload.h"
#include "goldfish/devices/hal_plug_factory.h"
#include "goldfish/devices/hal_plug_to_i_plug_adapter.h"
#include "goldfish/devices/internal/hal_plug.h"

namespace goldfish::devices {
namespace {

using ::goldfish::async::LibuvEventLoop;
using ::goldfish::async::ThreadedEventLoop;
using ::goldfish::async::testing::TestEventLoop;
using ::testing::Eq;
using ::testing::IsEmpty;

constexpr std::string_view kDevice = "qfake-snapshot-device";

// What a RecordingSocket saw; outlives the socket, which deletes itself on
// unplug as the cable contract requires.
struct SocketRecord {
    std::string sent;
    std::atomic<bool> unplugged{false};
};

class RecordingSocket : public cable::ISocket {
  public:
    explicit RecordingSocket(std::shared_ptr<SocketRecord> record) : record_(std::move(record)) {}
    void SendAsync(const void* data, size_t size) override {
        record_->sent.append(static_cast<const char*>(data), size);
    }
    cable::PlugPtr SwitchPlug(cable::PlugPtr new_plug) override { return new_plug; }
    cable::PlugPtr UnplugImpl() override {
        record_->unplugged = true;
        delete this;
        return {};
    }

  private:
    std::shared_ptr<SocketRecord> record_;
};

cable::SocketPtr MakeSocket(const std::shared_ptr<SocketRecord>& record) {
    return cable::SocketPtr(new RecordingSocket(record));
}

// A HalPlug whose snapshot state is the concatenation of everything it
// received. `kPoisonState` makes LoadFromSnapshot() fail.
class FakeSnapshotPlug : public HalPlug {
  public:
    static constexpr std::string_view kPoisonState = "poison";

    explicit FakeSnapshotPlug(bool supports_snapshot = true)
            : supports_snapshot_(supports_snapshot) {}

    void OnConnect() override { ++on_connect_calls; }
    void OnReceive(std::string_view data) override {
        if (block_receive) {
            receive_entered.Notify();
            receive_release.WaitForNotification();
        }
        state += data;
        received += data;
        if (!on_receive_send.empty()) {
            Socket()->Send(on_receive_send);
        }
    }
    void OnClose() override { ++on_close_calls; }

    bool SupportsSnapshot() const override { return supports_snapshot_; }
    bool SaveToSnapshot(archive::IWriter& writer) const override {
        writer << std::string_view(state);
        return true;
    }
    bool LoadFromSnapshot(archive::IReader& reader) override {
        return ReadValue(reader, state).ok() && state != kPoisonState;
    }
    void OnRestore() override {
        ++on_restore_calls;
        restore_thread = std::this_thread::get_id();
        Socket()->Send(on_restore_send);
    }

    std::shared_ptr<HalSocket> socket() { return Socket(); }

    std::string state;
    std::string received;
    std::string on_restore_send = "restored";
    std::string on_receive_send;
    int on_connect_calls = 0;
    int on_close_calls = 0;
    int on_restore_calls = 0;
    std::thread::id restore_thread;

    bool block_receive = false;
    absl::Notification receive_entered;
    absl::Notification receive_release;

  private:
    const bool supports_snapshot_;
};

class HalPlugSnapshotTest : public ::testing::Test {
  protected:
    void SetUp() override {
        client_loop_ = TestEventLoop::Create("client");
        qemu_loop_ = TestEventLoop::Create("qemu");
        HalPlugSnapshotRegistry::Get().Register(std::string(kDevice), RestoreEntry());
    }

    void TearDown() override {
        HalPlugSnapshotRegistry::Get().Unregister(kDevice);
        client_loop_->RunAll();
        qemu_loop_->RunAll();
    }

    HalPlugSnapshotRegistry::Entry RestoreEntry() {
        return {.client_loop = client_loop_.get(),
                .qemu_loop = qemu_loop_.get(),
                .factory = [this](std::string_view args) {
                    factory_args_.emplace_back(args);
                    restored_plug_ =
                            std::make_shared<FakeSnapshotPlug>(restored_supports_snapshot_);
                    return restored_plug_;
                }};
    }

    // Wraps `plug` the way ConnectorRegistry does for a guest connection.
    cable::PlugPtr Wrap(const std::shared_ptr<FakeSnapshotPlug>& plug,
                        const std::shared_ptr<SocketRecord>& record,
                        std::optional<HalPlugSnapshotIdentity> identity) {
        return HalPlugFactory::WrapHalPlug(
                MakeSocket(record), [plug] { return plug; }, client_loop_.get(), qemu_loop_.get(),
                std::move(identity));
    }

    static HalPlugSnapshotIdentity Identity(std::string args = "arg=1") {
        return {.device = std::string(kDevice), .args = std::move(args)};
    }

    std::unique_ptr<TestEventLoop> client_loop_;
    std::unique_ptr<TestEventLoop> qemu_loop_;
    std::vector<std::string> factory_args_;
    std::shared_ptr<FakeSnapshotPlug> restored_plug_;
    bool restored_supports_snapshot_ = true;
};

TEST_F(HalPlugSnapshotTest, NotSnapshottableWithoutIdentity) {
    auto plug = std::make_shared<FakeSnapshotPlug>();
    auto record = std::make_shared<SocketRecord>();
    cable::PlugPtr adapter = Wrap(plug, record, std::nullopt);

    EXPECT_FALSE(adapter->SupportsLoadingFromSnapshot());
    archive::DequeArchive archive;
    EXPECT_FALSE(cable::SavePlugToSnapshot(*adapter, archive));
    EXPECT_TRUE(archive.Empty());
}

TEST_F(HalPlugSnapshotTest, NotSnapshottableWhenPlugOptsOut) {
    auto plug = std::make_shared<FakeSnapshotPlug>(/*supports_snapshot=*/false);
    auto record = std::make_shared<SocketRecord>();
    cable::PlugPtr adapter = Wrap(plug, record, Identity());

    EXPECT_FALSE(adapter->SupportsLoadingFromSnapshot());
    archive::DequeArchive archive;
    EXPECT_FALSE(cable::SavePlugToSnapshot(*adapter, archive));
}

TEST_F(HalPlugSnapshotTest, SnapshottableWithIdentityAndOptIn) {
    auto plug = std::make_shared<FakeSnapshotPlug>();
    auto record = std::make_shared<SocketRecord>();
    cable::PlugPtr adapter = Wrap(plug, record, Identity());

    EXPECT_TRUE(adapter->SupportsLoadingFromSnapshot());
    EXPECT_EQ(adapter->GetSnapshotTypeId(), HalPlugToIPlugAdapter::kSnapshotTypeId);
}

TEST_F(HalPlugSnapshotTest, RoundTripRestoresStateUndeliveredBytesAndPendingSends) {
    auto plug = std::make_shared<FakeSnapshotPlug>();
    auto record = std::make_shared<SocketRecord>();
    cable::PlugPtr adapter = Wrap(plug, record, Identity("arg=1"));
    client_loop_->RunAll();  // OnConnect
    ASSERT_EQ(plug->on_connect_calls, 1);

    // State the plug has already consumed ...
    adapter->OnReceive("abc", 3);
    client_loop_->RunAll();
    ASSERT_EQ(plug->state, "abc");
    // ... guest bytes coalesced in the adapter but not delivered yet ...
    adapter->OnReceive("def", 3);
    // ... and host bytes coalesced in the marshalling socket but not flushed.
    plug->socket()->Send("host1");

    archive::DequeArchive archive;
    ASSERT_TRUE(cable::SavePlugToSnapshot(*adapter, archive));
    // Saving is non-destructive: the live connection keeps working.
    client_loop_->RunAll();
    qemu_loop_->RunAll();
    EXPECT_EQ(plug->state, "abcdef");
    EXPECT_EQ(record->sent, "host1");

    // Load through the real cable loader path (type id lookup + registry).
    auto record2 = std::make_shared<SocketRecord>();
    cable::PlugOrSocket loaded = cable::LoadPlugFromSnapshot(MakeSocket(record2), archive);
    ASSERT_TRUE(std::holds_alternative<cable::PlugPtr>(loaded));
    cable::PlugPtr adapter2 = std::get<cable::PlugPtr>(loaded);
    ASSERT_NE(adapter2, nullptr);
    ASSERT_NE(restored_plug_, nullptr);
    EXPECT_THAT(factory_args_, ::testing::ElementsAre("arg=1"));
    EXPECT_TRUE(adapter2->SupportsLoadingFromSnapshot());

    // Nothing touches the new plug until its loop runs.
    EXPECT_THAT(restored_plug_->state, IsEmpty());
    EXPECT_EQ(restored_plug_->on_restore_calls, 0);

    // Bytes the guest sends right after the load must queue behind the restore.
    adapter2->OnReceive("ghi", 3);

    client_loop_->RunAll();
    EXPECT_EQ(restored_plug_->on_restore_calls, 1);
    EXPECT_EQ(restored_plug_->on_connect_calls, 0) << "OnRestore replaces OnConnect";
    EXPECT_EQ(restored_plug_->state, "abcdefghi");
    EXPECT_EQ(restored_plug_->received, "defghi") << "undelivered bytes first, then new ones";

    qemu_loop_->RunAll();
    EXPECT_EQ(record2->sent, "host1restored") << "pending host bytes go out before OnRestore's";

    // Tear down the restored connection like a guest close would.
    adapter2->OnUnplug();
    client_loop_->RunAll();
    qemu_loop_->RunAll();
    EXPECT_EQ(restored_plug_->on_close_calls, 1);
    EXPECT_TRUE(record2->unplugged);
}

TEST_F(HalPlugSnapshotTest, EmptyBuffersRoundTrip) {
    auto plug = std::make_shared<FakeSnapshotPlug>();
    auto record = std::make_shared<SocketRecord>();
    cable::PlugPtr adapter = Wrap(plug, record, Identity(""));
    client_loop_->RunAll();

    archive::DequeArchive archive;
    ASSERT_TRUE(cable::SavePlugToSnapshot(*adapter, archive));

    auto record2 = std::make_shared<SocketRecord>();
    cable::PlugOrSocket loaded = cable::LoadPlugFromSnapshot(MakeSocket(record2), archive);
    ASSERT_TRUE(std::holds_alternative<cable::PlugPtr>(loaded));
    EXPECT_THAT(factory_args_, ::testing::ElementsAre(""));
    client_loop_->RunAll();
    EXPECT_EQ(restored_plug_->on_restore_calls, 1);
    EXPECT_THAT(restored_plug_->received, IsEmpty());
    EXPECT_TRUE(archive.Empty()) << "the loader consumed exactly what was saved";
}

TEST_F(HalPlugSnapshotTest, LoaderHandsSocketBackWhenDeviceIsNotRegistered) {
    auto plug = std::make_shared<FakeSnapshotPlug>();
    auto record = std::make_shared<SocketRecord>();
    cable::PlugPtr adapter = Wrap(plug, record, Identity());
    archive::DequeArchive archive;
    ASSERT_TRUE(cable::SavePlugToSnapshot(*adapter, archive));

    HalPlugSnapshotRegistry::Get().Unregister(kDevice);

    auto record2 = std::make_shared<SocketRecord>();
    cable::PlugOrSocket loaded = cable::LoadPlugFromSnapshot(MakeSocket(record2), archive);
    ASSERT_TRUE(std::holds_alternative<cable::SocketPtr>(loaded));
    EXPECT_EQ(restored_plug_, nullptr);
    // Give the socket back so RecordingSocket frees itself.
    std::get<cable::SocketPtr>(loaded).reset();
    EXPECT_TRUE(record2->unplugged);
}

TEST_F(HalPlugSnapshotTest, LoaderHandsSocketBackWhenRestoredPlugOptsOut) {
    auto plug = std::make_shared<FakeSnapshotPlug>();
    auto record = std::make_shared<SocketRecord>();
    cable::PlugPtr adapter = Wrap(plug, record, Identity());
    archive::DequeArchive archive;
    ASSERT_TRUE(cable::SavePlugToSnapshot(*adapter, archive));

    restored_supports_snapshot_ = false;
    auto record2 = std::make_shared<SocketRecord>();
    cable::PlugOrSocket loaded = cable::LoadPlugFromSnapshot(MakeSocket(record2), archive);
    ASSERT_TRUE(std::holds_alternative<cable::SocketPtr>(loaded));
    std::get<cable::SocketPtr>(loaded).reset();
}

TEST_F(HalPlugSnapshotTest, LoaderHandsSocketBackOnCorruptArchive) {
    archive::DequeArchive archive;
    archive << std::string_view(HalPlugToIPlugAdapter::kSnapshotTypeId);
    archive << uint32_t{1} << std::string_view(kDevice);  // truncated after the device
    auto record = std::make_shared<SocketRecord>();
    cable::PlugOrSocket loaded = cable::LoadPlugFromSnapshot(MakeSocket(record), archive);
    ASSERT_TRUE(std::holds_alternative<cable::SocketPtr>(loaded));
    std::get<cable::SocketPtr>(loaded).reset();

    archive::DequeArchive wrong_version;
    wrong_version << std::string_view(HalPlugToIPlugAdapter::kSnapshotTypeId);
    wrong_version << uint32_t{99} << std::string_view(kDevice) << std::string_view("")
                  << std::string_view("") << std::string_view("") << std::string_view("");
    loaded = cable::LoadPlugFromSnapshot(MakeSocket(record), wrong_version);
    ASSERT_TRUE(std::holds_alternative<cable::SocketPtr>(loaded));
    std::get<cable::SocketPtr>(loaded).reset();
    EXPECT_EQ(restored_plug_, nullptr);
}

TEST_F(HalPlugSnapshotTest, FailedPlugLoadClosesTheConnection) {
    auto plug = std::make_shared<FakeSnapshotPlug>();
    plug->state = FakeSnapshotPlug::kPoisonState;
    auto record = std::make_shared<SocketRecord>();
    cable::PlugPtr adapter = Wrap(plug, record, Identity());
    client_loop_->RunAll();  // OnConnect
    // Guest bytes coalesced but not yet delivered when the snapshot is taken.
    adapter->OnReceive("pending", 7);
    archive::DequeArchive archive;
    ASSERT_TRUE(cable::SavePlugToSnapshot(*adapter, archive));

    auto record2 = std::make_shared<SocketRecord>();
    cable::PlugOrSocket loaded = cable::LoadPlugFromSnapshot(MakeSocket(record2), archive);
    ASSERT_TRUE(std::holds_alternative<cable::PlugPtr>(loaded));
    // Bytes the guest sends right after the load, before the restore task ran.
    std::get<cable::PlugPtr>(loaded)->OnReceive("late", 4);
    client_loop_->RunAll();
    EXPECT_EQ(restored_plug_->on_restore_calls, 0);
    // A plug whose LoadFromSnapshot() failed must not see any callback: neither
    // the bytes saved in the snapshot nor the ones that arrived after the load.
    EXPECT_THAT(restored_plug_->received, IsEmpty());
    qemu_loop_->RunAll();
    EXPECT_TRUE(record2->unplugged) << "the guest must see the connection go away";
}

TEST_F(HalPlugSnapshotTest, RegistryReplacesAndUnregisters) {
    HalPlugSnapshotRegistry registry;
    EXPECT_FALSE(registry.Find("x").has_value());

    int which = 0;
    registry.Register("x", {.client_loop = client_loop_.get(),
                            .qemu_loop = qemu_loop_.get(),
                            .factory = [&](std::string_view) {
                                which = 1;
                                return nullptr;
                            }});
    registry.Register("x", {.client_loop = client_loop_.get(),
                            .qemu_loop = qemu_loop_.get(),
                            .factory = [&](std::string_view) {
                                which = 2;
                                return nullptr;
                            }});
    auto entry = registry.Find("x");
    ASSERT_TRUE(entry.has_value());
    entry->factory("");
    EXPECT_EQ(which, 2);

    registry.Unregister("x");
    EXPECT_FALSE(registry.Find("x").has_value());
    registry.Unregister("x");  // idempotent
}

// Lock order: the vsock layer calls SaveStateToSnapshot() *and* ISocket::SendAsync()
// under its state mutex. Save takes callback_mutex_, and a HalPlug callback that
// Send()s takes the marshalling socket's mutex under callback_mutex_. If the
// marshalling socket's flush task called SendAsync() while still holding its own
// mutex, the three orders would form a cycle:
//   vsock -> callback_mutex_ -> socket_mutex_ -> vsock
// absl::Mutex's deadlock detector aborts the process when it sees such a cycle,
// which is exactly what this test provokes by exercising every edge in one binary.
TEST(HalPlugSnapshotLockOrderTest, SaveUnderTransportLockDoesNotInvertWithFlush) {
    // Stands in for vsock's state mutex.
    struct LockingSocket : public cable::ISocket {
        absl::Mutex state_mutex;
        std::string sent;
        void SendAsync(const void* data, size_t size) override {
            const absl::MutexLock lock(&state_mutex);
            sent.append(static_cast<const char*>(data), size);
        }
        cable::PlugPtr SwitchPlug(cable::PlugPtr new_plug) override { return new_plug; }
        cable::PlugPtr UnplugImpl() override { return {}; }
    };
    LockingSocket socket;

    auto client_loop = TestEventLoop::Create("client");
    auto qemu_loop = TestEventLoop::Create("qemu");
    auto plug = std::make_shared<FakeSnapshotPlug>();
    cable::PlugPtr adapter = HalPlugFactory::WrapHalPlug(
            cable::SocketPtr(&socket), [plug] { return plug; }, client_loop.get(), qemu_loop.get(),
            HalPlugSnapshotIdentity{.device = std::string(kDevice), .args = ""});
    client_loop->RunAll();  // OnConnect

    // Edge 1 (client loop): callback_mutex_ -> socket_mutex_.
    plug->on_receive_send = "reply";
    adapter->OnReceive("ping", 4);
    client_loop->RunAll();
    // Edge 2 (QEMU loop): flush -> SendAsync -> transport lock.
    qemu_loop->RunAll();
    EXPECT_EQ(socket.sent, "reply");

    // Edge 3 (QEMU thread): transport lock -> callback_mutex_ (-> socket_mutex_).
    archive::DequeArchive archive;
    {
        const absl::MutexLock lock(&socket.state_mutex);
        EXPECT_TRUE(adapter->SaveStateToSnapshot(archive));
    }
    EXPECT_FALSE(archive.Empty());

    // Detach the stack socket before the marshalling socket goes away.
    adapter->OnUnplug().release();
    client_loop->RunAll();
    qemu_loop->RunAll();
}

// Concurrency: SaveStateToSnapshot() runs on the QEMU thread while the client
// loop may be inside HalPlug::OnReceive(). The delivery task has already moved
// the bytes out of the adapter's receive buffer, so a save that does not wait
// for the callback loses them. Without callback_mutex_ this test fails on the
// final EXPECT (and TSAN reports the deliver_buffer_ race).
TEST(HalPlugSnapshotConcurrencyTest, SaveWaitsForInFlightOnReceive) {
    auto client_loop = ThreadedEventLoop::Create(LibuvEventLoop::Create());
    auto qemu_loop = ThreadedEventLoop::Create(LibuvEventLoop::Create());

    auto plug = std::make_shared<FakeSnapshotPlug>();
    plug->block_receive = true;
    auto record = std::make_shared<SocketRecord>();
    cable::PlugPtr adapter = HalPlugFactory::WrapHalPlug(
            MakeSocket(record), [plug] { return plug; }, client_loop.get(), qemu_loop.get(),
            HalPlugSnapshotIdentity{.device = std::string(kDevice), .args = ""});

    // Park the client loop inside OnReceive("abc").
    adapter->OnReceive("abc", 3);
    ASSERT_TRUE(plug->receive_entered.WaitForNotificationWithTimeout(absl::Seconds(5)));

    archive::DequeArchive archive;
    absl::Notification save_done;
    bool saved = false;
    std::thread saver([&] {
        saved = cable::SavePlugToSnapshot(*adapter, archive);
        save_done.Notify();
    });

    // The save must block until OnReceive returns.
    EXPECT_FALSE(save_done.WaitForNotificationWithTimeout(absl::Milliseconds(100)));
    plug->receive_release.Notify();
    ASSERT_TRUE(save_done.WaitForNotificationWithTimeout(absl::Seconds(5)));
    saver.join();
    ASSERT_TRUE(saved);

    // Skip the type id, then the adapter header, to get at the plug blob.
    std::string type_id;
    uint32_t version = 0;
    std::string device;
    std::string args;
    std::string pending_send;
    std::string pending_receive;
    std::string plug_state;
    ASSERT_THAT(ReadValue(archive, type_id, version, device, args, pending_send, pending_receive,
                          plug_state),
                ::absl_testing::IsOk());
    archive::DequeArchive plug_archive;
    plug_archive.storage.assign(plug_state.begin(), plug_state.end());
    std::string state;
    ASSERT_THAT(ReadValue(plug_archive, state), ::absl_testing::IsOk());
    EXPECT_EQ(state + pending_receive, "abc")
            << "bytes in flight between the adapter and the plug were lost";
    EXPECT_EQ(state, "abc");

    // Clean shutdown: close from the client side and let both loops drain.
    client_loop->Post([&] { plug->socket()->Close(); }).IgnoreError();
    for (int i = 0; i < 100 && !record->unplugged; ++i) {
        absl::SleepFor(absl::Milliseconds(10));
    }
    adapter.reset();
    client_loop->ShutdownAndWait(absl::Seconds(2)).IgnoreError();
    qemu_loop->ShutdownAndWait(absl::Seconds(2)).IgnoreError();
}

}  // namespace
}  // namespace goldfish::devices
