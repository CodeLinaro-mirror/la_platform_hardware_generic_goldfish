// Copyright (C) 2026 The Android Open Source Project
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

#include "snapshot_util.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status.h"

#include "mock_avd.h"
#include "goldfish/async/testing/fake_async_socket.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/network/endpoint.h"

namespace android::goldfish {
namespace {

using testing::_;
using testing::Return;
using testing::StrictMock;

class MockAsyncSocketFactory : public ::goldfish::async::AsyncSocketFactory {
  public:
    MOCK_METHOD(std::shared_ptr<::goldfish::async::AsyncSocketServer>, CreateServer,
                (::goldfish::async::EventLoop * loop, const ::goldfish::network::Endpoint& endpoint,
                 ::goldfish::async::AsyncSocketServer::ConnectCallback connect_callback,
                 ::goldfish::async::AsyncSocketServer::LoopProvider loop_provider),
                (override));

    MOCK_METHOD(std::shared_ptr<::goldfish::async::AsyncSocket>, CreateSocket,
                (::goldfish::async::EventLoop * loop,
                 const ::goldfish::network::Endpoint& endpoint),
                (override));
};

class FakeAsyncSocketWithSentData : public ::goldfish::async::testing::FakeAsyncSocket {
  public:
    absl::Status Send(const char* buffer, size_t buffer_size, OnSendCallback on_send) override {
        sent_data_.append(buffer, buffer_size);
        if (on_send) {
            (void)event_loop_->Post(
                    [on_send = std::move(on_send)]() { on_send(absl::OkStatus()); });
        }
        return absl::OkStatus();
    }

    const std::string& sent_data() const { return sent_data_; }
    void clear_sent_data() { sent_data_.clear(); }

  private:
    std::string sent_data_;
};

class SnapshotUtilTest : public testing::Test {
  protected:
    void SetUp() override {
        event_loop = ::goldfish::async::testing::TestEventLoop::Create();
        fake_socket = std::make_shared<FakeAsyncSocketWithSentData>();
        fake_socket->setEventLoop(event_loop.get());

        avd = std::make_unique<StrictMock<MockAvd>>();
        temp_dir = std::filesystem::path(testing::TempDir()) / "snapshot_util_test";
        std::filesystem::remove_all(temp_dir);
        std::filesystem::create_directories(temp_dir);

        EXPECT_CALL(*avd, GetContentPath()).WillRepeatedly(Return(temp_dir));
    }

    void TearDown() override { std::filesystem::remove_all(temp_dir); }

    std::unique_ptr<::goldfish::async::testing::TestEventLoop> event_loop;
    std::shared_ptr<FakeAsyncSocketWithSentData> fake_socket;
    StrictMock<MockAsyncSocketFactory> mock_factory;
    std::unique_ptr<MockAvd> avd;
    std::filesystem::path temp_dir;

    bool kill_called = false;
    void kill_emulator() { kill_called = true; }
};

TEST_F(SnapshotUtilTest, SuccessfulSnapshot) {
    EXPECT_CALL(mock_factory, CreateSocket(_, _)).WillOnce(Return(fake_socket));

    SnapshotUtil::save_snapshot_and_quit(*event_loop, mock_factory, 1234, "test_snap", avd.get(),
                                         [this]() { kill_emulator(); });

    event_loop->RunAll();
    fake_socket->SimulateConnected(absl::OkStatus());
    event_loop->RunAll();

    // Greeting
    fake_socket->SimulateRead("{\"QMP\": {}}\n");
    event_loop->RunAll();
    EXPECT_EQ(fake_socket->sent_data(), "{\"execute\": \"qmp_capabilities\"}\n");
    fake_socket->clear_sent_data();

    // Capabilities return
    fake_socket->SimulateRead("{\"return\": {}}\n");
    event_loop->RunAll();
    EXPECT_THAT(fake_socket->sent_data(), testing::HasSubstr("savevm test_snap"));
    fake_socket->clear_sent_data();

    // Snapshot return
    fake_socket->SimulateRead("{\"return\": \"OK\"}\n");
    event_loop->RunAll();
    EXPECT_EQ(fake_socket->sent_data(), "{\"execute\": \"quit\"}\n");
    EXPECT_FALSE(kill_called);

    // QEMU exits, closing the socket
    fake_socket->SimulateError(absl::CancelledError("closed"));
    event_loop->RunAll();
    EXPECT_TRUE(kill_called);
}

TEST_F(SnapshotUtilTest, ConnectionFailure) {
    EXPECT_CALL(mock_factory, CreateSocket(_, _)).WillOnce(Return(fake_socket));

    SnapshotUtil::save_snapshot_and_quit(*event_loop, mock_factory, 1234, "test_snap", avd.get(),
                                         [this]() { kill_emulator(); });

    event_loop->RunAll();
    fake_socket->SimulateConnected(absl::InternalError("connection failed"));
    event_loop->RunAll();

    EXPECT_TRUE(kill_called);
}

TEST_F(SnapshotUtilTest, SnapshotFailureDeletesDirectory) {
    EXPECT_CALL(mock_factory, CreateSocket(_, _)).WillOnce(Return(fake_socket));

    std::filesystem::path snap_dir = temp_dir / "snapshots" / "test_snap";
    std::filesystem::create_directories(snap_dir);
    ASSERT_TRUE(std::filesystem::exists(snap_dir));

    SnapshotUtil::save_snapshot_and_quit(*event_loop, mock_factory, 1234, "test_snap", avd.get(),
                                         [this]() { kill_emulator(); });

    event_loop->RunAll();
    fake_socket->SimulateConnected(absl::OkStatus());
    event_loop->RunAll();
    fake_socket->SimulateRead("{\"QMP\": {}}\n");
    event_loop->RunAll();
    fake_socket->SimulateRead("{\"return\": {}}\n");
    event_loop->RunAll();

    // Fail the snapshot
    fake_socket->SimulateRead("{\"error\": \"failed to save\"}\n");
    event_loop->RunAll();

    EXPECT_THAT(fake_socket->sent_data(), testing::EndsWith("{\"execute\": \"quit\"}\n"));
    EXPECT_FALSE(std::filesystem::exists(snap_dir));

    fake_socket->SimulateError(absl::CancelledError("closed"));
    event_loop->RunAll();
    EXPECT_TRUE(kill_called);
}

TEST_F(SnapshotUtilTest, TimeoutTriggersQuit) {
    EXPECT_CALL(mock_factory, CreateSocket(_, _)).WillOnce(Return(fake_socket));

    SnapshotUtil::save_snapshot_and_quit(*event_loop, mock_factory, 1234, "test_snap", avd.get(),
                                         [this]() { kill_emulator(); });

    event_loop->RunAll();
    fake_socket->SimulateConnected(absl::OkStatus());
    event_loop->RunAll();
    fake_socket->SimulateRead("{\"QMP\": {}}\n");
    event_loop->RunAll();
    fake_socket->SimulateRead("{\"return\": {}}\n");
    event_loop->RunAll();

    // Advance clock past 5 minutes
    event_loop->AdvanceClock(std::chrono::minutes(6));
    event_loop->RunAll();

    EXPECT_THAT(fake_socket->sent_data(), testing::EndsWith("{\"execute\": \"quit\"}\n"));

    fake_socket->SimulateError(absl::CancelledError("closed"));
    event_loop->RunAll();
    EXPECT_TRUE(kill_called);
}

}  // namespace
}  // namespace android::goldfish
