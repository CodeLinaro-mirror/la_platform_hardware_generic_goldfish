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

#include "system_service_impl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/time/time.h"

#include "android/goldfish/vm_interface.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/avd_universe/clipboard/clipboard_data.h"
#include "goldfish/avd_universe/fingerprint/fingerprint_sensor.h"
#include "goldfish/avd_universe/grpc/grpc_notification_channel.h"
#include "goldfish/avd_universe/guest_status/guest_status.h"
#include "system_service.grpc.pb.h"
#include "test/grpc_v2_service_test.h"

namespace goldfish::grpc::v2 {
namespace {

using ::android::emulation::v2::ClipboardState;
using ::android::emulation::v2::GetClipboardStateRequest;
using ::android::emulation::v2::GetSystemStateRequest;
using ::android::emulation::v2::InjectFingerprintRequest;
using ::android::emulation::v2::InjectFingerprintResponse;
using ::android::emulation::v2::RebootDeviceRequest;
using ::android::emulation::v2::RebootDeviceResponse;
using ::android::emulation::v2::SystemState;
using ::android::emulation::v2::UpdateClipboardStateRequest;
using ::android::goldfish::EmuRunState;
using ::android::goldfish::QemuShutdownCause;
using ::android::goldfish::VmConfiguration;
using ::android::goldfish::VmOperations;
using ::goldfish::avd_universe::clipboard::ClipboardChannel;
using ::goldfish::avd_universe::fingerprint::kReleaseEvent;
using ::goldfish::avd_universe::fingerprint::ObservableFingerprintSensor;
using ::goldfish::avd_universe::grpc::GrpcNotificationEventSource;
using ::goldfish::avd_universe::guest_status::GuestStatus;
using ::testing::ElementsAre;

class MockVmOps : public VmOperations {
  public:
    explicit MockVmOps(::goldfish::async::EventLoop* loop) : VmOperations(loop) {}

    MOCK_METHOD(absl::Status, Stop, (), (override));
    MOCK_METHOD(absl::Status, Start, (), (override));
    MOCK_METHOD(absl::Status, Reset, (), (override));
    MOCK_METHOD(absl::Status, Shutdown, (), (override));
    MOCK_METHOD(absl::Status, Pause, (), (override));
    MOCK_METHOD(absl::Status, Resume, (), (override));
    MOCK_METHOD(bool, IsRunning, (), (override));
    MOCK_METHOD(VmConfiguration, GetConfiguration, (), (override));
    MOCK_METHOD(EmuRunState, GetRunState, (), (override));
    MOCK_METHOD(void, SystemShutdownRequest, (QemuShutdownCause reason), (override));
    MOCK_METHOD(absl::Status, SaveSnapshot, (std::string name, bool overwrite), (override));
    MOCK_METHOD(absl::Status, LoadSnapshot, (std::string id_or_name, bool and_resume), (override));
    MOCK_METHOD(void, LoadSnapshotResume, (EmuRunState), (override));
    MOCK_METHOD(absl::Status, DeleteSnapshot, (std::string id_or_name), (override));
    MOCK_METHOD(absl::Status, ListSnapshots, (SnapshotEntrySink), (override));
};

class SystemServiceTest : public ::testing::Test {
  protected:
    void SetUp() override {
        guest_status_.Reset(absl::UnixEpoch());
        guest_status_.SetGrpcNotificationChannel(&notification_source_);
        mock_vm_ = std::make_unique<MockVmOps>(loop_.get());
        service_ = std::make_unique<SystemServiceImpl>(guest_status_, &notification_source_,
                                                       mock_vm_.get(), &clipboard_channel_,
                                                       &fingerprint_sensor_, absl::ZeroDuration());
    }

    const std::unique_ptr<::goldfish::async::testing::TestEventLoop> loop_ =
            ::goldfish::async::testing::TestEventLoop::Create();
    GuestStatus guest_status_;
    GrpcNotificationEventSource notification_source_;
    ClipboardChannel clipboard_channel_;
    ObservableFingerprintSensor fingerprint_sensor_;
    std::unique_ptr<MockVmOps> mock_vm_;
    std::unique_ptr<SystemServiceImpl> service_;
};

TEST_F(SystemServiceTest, ReportsBootingBeforeBootComplete) {
    GetSystemStateRequest req;
    SystemState reply;
    ::grpc::ServerContext ctx;

    auto status = service_->GetSystemState(&ctx, &req, &reply);
    ASSERT_TRUE(status.ok()) << status.error_message();

    EXPECT_EQ(reply.lifecycle_state(), SystemState::BOOTING);
    EXPECT_EQ(reply.connection_state(), SystemState::DISCONNECTED);
    EXPECT_EQ(reply.boot_completed_duration_ms(), 0);
}

TEST_F(SystemServiceTest, ReportsBootCompletedAfterNotification) {
    absl::Time start = absl::UnixEpoch();
    guest_status_.Reset(start);
    guest_status_.SetBootComplete(start + absl::Seconds(15));
    guest_status_.Heartbeat();
    guest_status_.Heartbeat();

    GetSystemStateRequest req;
    SystemState reply;
    ::grpc::ServerContext ctx;

    auto status = service_->GetSystemState(&ctx, &req, &reply);
    ASSERT_TRUE(status.ok());

    EXPECT_EQ(reply.lifecycle_state(), SystemState::BOOT_COMPLETED);
    EXPECT_EQ(reply.connection_state(), SystemState::CONNECTED);
    EXPECT_EQ(reply.boot_completed_duration_ms(), 15000);
    EXPECT_EQ(reply.heartbeat_counter(), 2);
}

TEST_F(SystemServiceTest, StreamSystemStateReactivelyReceivesUpdates) {
    auto* writer = service_->CreateSystemStateWriter<test::FakeWriteReactor<SystemState>>();

    // Initial state delivered immediately on connect
    ASSERT_EQ(writer->written_messages.size(), 1);
    EXPECT_EQ(writer->written_messages[0].lifecycle_state(), SystemState::BOOTING);
    EXPECT_EQ(writer->written_messages[0].connection_state(), SystemState::DISCONNECTED);
    writer->OnWriteDone(true);

    // Trigger guest boot completion event via GuestStatus -> GrpcNotificationEventSource
    absl::Time start = absl::UnixEpoch();
    guest_status_.SetBootComplete(start + absl::Seconds(12));

    ASSERT_EQ(writer->written_messages.size(), 2);
    EXPECT_EQ(writer->written_messages[1].lifecycle_state(), SystemState::BOOT_COMPLETED);
    EXPECT_EQ(writer->written_messages[1].connection_state(), SystemState::CONNECTED);
    EXPECT_EQ(writer->written_messages[1].boot_completed_duration_ms(), 12000);

    writer->OnDone();
}

TEST_F(SystemServiceTest, RebootDeviceTriggersVmReset) {
    EXPECT_CALL(*mock_vm_, Reset()).WillOnce(::testing::Return(absl::OkStatus()));

    RebootDeviceRequest req;
    RebootDeviceResponse reply;
    ::grpc::ServerContext ctx;

    auto status = service_->RebootDevice(&ctx, &req, &reply);
    ASSERT_TRUE(status.ok());
}

TEST_F(SystemServiceTest, GetClipboardStateInitiallyEmpty) {
    GetClipboardStateRequest req;
    ClipboardState reply;
    ::grpc::ServerContext ctx;

    auto status = service_->GetClipboardState(&ctx, &req, &reply);
    ASSERT_TRUE(status.ok()) << status.error_message();
    EXPECT_EQ(reply.text(), "");
    EXPECT_EQ(reply.mime_type(), "");
    EXPECT_FALSE(reply.has_binary_data());
}

TEST_F(SystemServiceTest, UpdateClipboardStateForwardsToGuestAndUpdatesState) {
    UpdateClipboardStateRequest update_req;
    update_req.set_text("Hello Android");
    ClipboardState update_reply;
    ::grpc::ServerContext ctx;

    auto status = service_->UpdateClipboardState(&ctx, &update_req, &update_reply);
    ASSERT_TRUE(status.ok()) << status.error_message();
    EXPECT_EQ(update_reply.text(), "Hello Android");
    EXPECT_EQ(update_reply.mime_type(), "text/plain");
    EXPECT_EQ(clipboard_channel_.host_to_guest.GetValue().contents, "Hello Android");

    GetClipboardStateRequest get_req;
    ClipboardState get_reply;
    ASSERT_TRUE(service_->GetClipboardState(&ctx, &get_req, &get_reply).ok());
    EXPECT_EQ(get_reply.text(), "Hello Android");
    EXPECT_EQ(get_reply.mime_type(), "text/plain");

    // Custom MIME type and binary data
    UpdateClipboardStateRequest rich_req;
    rich_req.set_text("<b>Bold</b>");
    rich_req.set_mime_type("text/html");
    rich_req.set_binary_data("\x89PNG");
    ClipboardState rich_reply;
    ASSERT_TRUE(service_->UpdateClipboardState(&ctx, &rich_req, &rich_reply).ok());
    EXPECT_EQ(rich_reply.text(), "<b>Bold</b>");
    EXPECT_EQ(rich_reply.mime_type(), "text/html");
    EXPECT_TRUE(rich_reply.has_binary_data());
    EXPECT_EQ(rich_reply.binary_data(), "\x89PNG");

    // Clearing clipboard
    UpdateClipboardStateRequest clear_req;
    ClipboardState clear_reply;
    ASSERT_TRUE(service_->UpdateClipboardState(&ctx, &clear_req, &clear_reply).ok());
    EXPECT_EQ(clear_reply.text(), "");
    EXPECT_EQ(clear_reply.mime_type(), "");
    EXPECT_FALSE(clear_reply.has_binary_data());
    EXPECT_EQ(clipboard_channel_.host_to_guest.GetValue().contents, "");
}

TEST_F(SystemServiceTest, StreamClipboardStateReceivesInitialAndGuestUpdates) {
    auto* writer = service_->CreateClipboardStateWriter<test::FakeWriteReactor<ClipboardState>>();

    ASSERT_EQ(writer->written_messages.size(), 1);
    EXPECT_EQ(writer->written_messages[0].text(), "");
    EXPECT_EQ(writer->written_messages[0].mime_type(), "");
    writer->OnWriteDone(true);

    clipboard_channel_.guest_to_host.SetValue({.contents = "Copied from guest app"});

    ASSERT_EQ(writer->written_messages.size(), 2);
    EXPECT_EQ(writer->written_messages[1].text(), "Copied from guest app");
    EXPECT_EQ(writer->written_messages[1].mime_type(), "text/plain");

    GetClipboardStateRequest get_req;
    ClipboardState get_reply;
    ::grpc::ServerContext ctx;
    ASSERT_TRUE(service_->GetClipboardState(&ctx, &get_req, &get_reply).ok());
    EXPECT_EQ(get_reply.text(), "Copied from guest app");
    EXPECT_EQ(get_reply.mime_type(), "text/plain");

    writer->OnDone();
}

TEST_F(SystemServiceTest, InjectFingerprintExplicitTouchAndRelease) {
    std::vector<int64_t> events;
    auto cb_id =
            fingerprint_sensor_.AddCallback([&events](int64_t event) { events.push_back(event); });

    ::grpc::ServerContext ctx;
    InjectFingerprintRequest touch_req;
    touch_req.set_finger_id(2);
    touch_req.set_touching(true);
    InjectFingerprintResponse reply;

    ASSERT_TRUE(service_->InjectFingerprint(&ctx, &touch_req, &reply).ok());
    EXPECT_THAT(events, ElementsAre(2));

    InjectFingerprintRequest release_req;
    release_req.set_finger_id(2);
    release_req.set_touching(false);
    ASSERT_TRUE(service_->InjectFingerprint(&ctx, &release_req, &reply).ok());
    EXPECT_THAT(events, ElementsAre(2, kReleaseEvent));

    fingerprint_sensor_.RemoveCallback(cb_id);
}

TEST_F(SystemServiceTest, InjectFingerprintOmittedTouchingExecutesAtomicTouchAndReleaseCycle) {
    std::vector<int64_t> events;
    auto cb_id =
            fingerprint_sensor_.AddCallback([&events](int64_t event) { events.push_back(event); });

    ::grpc::ServerContext ctx;
    InjectFingerprintRequest scan_req;
    scan_req.set_finger_id(1);
    InjectFingerprintResponse reply;

    ASSERT_TRUE(service_->InjectFingerprint(&ctx, &scan_req, &reply).ok());
    EXPECT_THAT(events, ElementsAre(1, kReleaseEvent));

    fingerprint_sensor_.RemoveCallback(cb_id);
}

}  // namespace
}  // namespace goldfish::grpc::v2
