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
#include "screen_record_commands.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>

#include "android/base/testing/TestTempDir.h"
#include "emulator_controller_mock.grpc.pb.h"
#include "legacy_console_bridge.h"
#include "screen_recording_service.grpc.pb.h"

namespace goldfish::telnet {
namespace {

using android::emulation::control::incubating::RecordingInfo;
using android::emulation::control::incubating::ScreenRecording;
using testing::_;

class MockScreenRecordingStub : public ScreenRecording::StubInterface {
  public:
    MOCK_METHOD(grpc::Status, StartRecording,
                (grpc::ClientContext * context, const RecordingInfo& request,
                 RecordingInfo* response),
                (override));
    MOCK_METHOD(grpc::Status, StopRecording,
                (grpc::ClientContext * context, const RecordingInfo& request,
                 RecordingInfo* response),
                (override));
    MOCK_METHOD(grpc::Status, ListRecordings,
                (grpc::ClientContext * context, const RecordingInfo& request,
                 android::emulation::control::incubating::RecordingInfoList* response),
                (override));
    MOCK_METHOD((grpc::ClientReaderInterface<RecordingInfo>*), ReceiveRecordingEventsRaw,
                (grpc::ClientContext * context, const google::protobuf::Empty& request),
                (override));

    // Async methods stubs to satisfy pure virtual requirements
    ::grpc::ClientAsyncResponseReaderInterface<
            ::android::emulation::control::incubating::RecordingInfo>*
    AsyncStartRecordingRaw(::grpc::ClientContext* context,
                           const ::android::emulation::control::incubating::RecordingInfo& request,
                           ::grpc::CompletionQueue* cq) override {
        return nullptr;
    }
    ::grpc::ClientAsyncResponseReaderInterface<
            ::android::emulation::control::incubating::RecordingInfo>*
    PrepareAsyncStartRecordingRaw(
            ::grpc::ClientContext* context,
            const ::android::emulation::control::incubating::RecordingInfo& request,
            ::grpc::CompletionQueue* cq) override {
        return nullptr;
    }
    ::grpc::ClientAsyncResponseReaderInterface<
            ::android::emulation::control::incubating::RecordingInfo>*
    AsyncStopRecordingRaw(::grpc::ClientContext* context,
                          const ::android::emulation::control::incubating::RecordingInfo& request,
                          ::grpc::CompletionQueue* cq) override {
        return nullptr;
    }
    ::grpc::ClientAsyncResponseReaderInterface<
            ::android::emulation::control::incubating::RecordingInfo>*
    PrepareAsyncStopRecordingRaw(
            ::grpc::ClientContext* context,
            const ::android::emulation::control::incubating::RecordingInfo& request,
            ::grpc::CompletionQueue* cq) override {
        return nullptr;
    }
    ::grpc::ClientAsyncResponseReaderInterface<
            ::android::emulation::control::incubating::RecordingInfoList>*
    AsyncListRecordingsRaw(::grpc::ClientContext* context,
                           const ::android::emulation::control::incubating::RecordingInfo& request,
                           ::grpc::CompletionQueue* cq) override {
        return nullptr;
    }
    ::grpc::ClientAsyncResponseReaderInterface<
            ::android::emulation::control::incubating::RecordingInfoList>*
    PrepareAsyncListRecordingsRaw(
            ::grpc::ClientContext* context,
            const ::android::emulation::control::incubating::RecordingInfo& request,
            ::grpc::CompletionQueue* cq) override {
        return nullptr;
    }
    ::grpc::ClientAsyncReaderInterface<::android::emulation::control::incubating::RecordingInfo>*
    AsyncReceiveRecordingEventsRaw(::grpc::ClientContext* context,
                                   const ::google::protobuf::Empty& request,
                                   ::grpc::CompletionQueue* cq, void* tag) override {
        return nullptr;
    }
    ::grpc::ClientAsyncReaderInterface<::android::emulation::control::incubating::RecordingInfo>*
    PrepareAsyncReceiveRecordingEventsRaw(::grpc::ClientContext* context,
                                          const ::google::protobuf::Empty& request,
                                          ::grpc::CompletionQueue* cq) override {
        return nullptr;
    }
};

struct MockConsoleContext : public LegacyConsoleBridge::ConsoleContext {
    explicit MockConsoleContext(int port) : ConsoleContext(port) {}

    absl::StatusOr<std::unique_ptr<
            android::emulation::control::incubating::ScreenRecording::StubInterface>>
    ScreenRecordingStub() override {
        if (mock_stub) {
            return std::move(mock_stub);
        }
        return absl::NotFoundError("No mock stub set");
    }

    absl::StatusOr<std::unique_ptr<android::emulation::control::EmulatorController::StubInterface>>
    EmulatorControllerStub() override {
        if (mock_emu_stub) {
            return std::move(mock_emu_stub);
        }
        return absl::NotFoundError("No mock emu stub set");
    }

    absl::StatusOr<std::unique_ptr<grpc::ClientContext>> NewContext(
            std::chrono::time_point<std::chrono::system_clock> deadline =
                    std::chrono::system_clock::now() + std::chrono::milliseconds(500)) override {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(deadline);
        return ctx;
    }

    std::unique_ptr<MockScreenRecordingStub> mock_stub;
    std::unique_ptr<android::emulation::control::MockEmulatorControllerStub> mock_emu_stub;
};

TEST(ScreenRecordCommandsTest, ParseStartCommand) {
    CommandRegistryBuilder builder("/dummy/token");
    auto screenrecord = builder.Command("screenrecord", "desc");
    RegisterScreenRecordCommands(screenrecord);
    auto registry = builder.Build();

    MockConsoleContext ctx(5554);
    ctx.authenticated = true;
    auto mock_stub = std::make_unique<MockScreenRecordingStub>();

    EXPECT_CALL(*mock_stub, StartRecording(_, _, _))
            .WillOnce([](grpc::ClientContext*, const RecordingInfo& request, RecordingInfo*) {
                EXPECT_EQ(request.file_name(), "test.webm");
                EXPECT_EQ(request.width(), 1280);
                EXPECT_EQ(request.height(), 720);
                EXPECT_EQ(request.bit_rate(), 4000000);
                EXPECT_EQ(request.time_limit(), 180);
                EXPECT_EQ(request.fps(), 24);
                EXPECT_EQ(request.display(), 0);
                return grpc::Status::OK;
            });

    ctx.mock_stub = std::move(mock_stub);

    auto result = (*registry)(
            "screenrecord start --size 1280x720 --bit-rate 4M --time-limit 180 "
            "--fps 24 --display 0 test.webm",
            ctx);
    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST(ScreenRecordCommandsTest, ParseStopCommand) {
    CommandRegistryBuilder builder("/dummy/token");
    auto screenrecord = builder.Command("screenrecord", "desc");
    RegisterScreenRecordCommands(screenrecord);
    auto registry = builder.Build();

    MockConsoleContext ctx(5554);
    ctx.authenticated = true;
    auto mock_stub = std::make_unique<MockScreenRecordingStub>();

    // Mock ListRecordings to return one active and one stopped recording
    EXPECT_CALL(*mock_stub, ListRecordings(_, _, _))
            .WillOnce([](grpc::ClientContext*, const RecordingInfo&,
                         android::emulation::control::incubating::RecordingInfoList* response) {
                auto* rec1 = response->add_recordings();
                rec1->set_file_name("active.webm");
                rec1->set_state(android::emulation::control::incubating::RecordingInfo::
                                        RECORDER_STATE_RECORDING);

                auto* rec2 = response->add_recordings();
                rec2->set_file_name("stopped.webm");
                rec2->set_state(android::emulation::control::incubating::RecordingInfo::
                                        RECORDER_STATE_STOPPED);

                return grpc::Status::OK;
            });

    // Expect StopRecording to be called ONLY for the active recording
    EXPECT_CALL(*mock_stub, StopRecording(_, _, _))
            .WillOnce([](grpc::ClientContext*, const RecordingInfo& request, RecordingInfo*) {
                EXPECT_EQ(request.file_name(), "active.webm");
                return grpc::Status::OK;
            });

    ctx.mock_stub = std::move(mock_stub);

    auto result = (*registry)("screenrecord stop", ctx);
    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST(ScreenRecordCommandsTest, ParseScreenshotCommand) {
    CommandRegistryBuilder builder("/dummy/token");
    auto screenrecord = builder.Command("screenrecord", "desc");
    RegisterScreenRecordCommands(screenrecord);
    auto registry = builder.Build();

    MockConsoleContext ctx(5554);
    ctx.authenticated = true;
    auto mock_emu_stub =
            std::make_unique<android::emulation::control::MockEmulatorControllerStub>();

    EXPECT_CALL(*mock_emu_stub, getScreenshot(_, _, _))
            .WillOnce([](grpc::ClientContext*,
                         const android::emulation::control::ImageFormat& request,
                         android::emulation::control::Image* response) {
                EXPECT_EQ(request.format(), android::emulation::control::ImageFormat::PNG);
                EXPECT_EQ(request.display(), 1);
                response->set_image("fake png data");
                return grpc::Status::OK;
            });

    ctx.mock_emu_stub = std::move(mock_emu_stub);

    android::base::TestTempDir tmp_dir("screenrecord_test");
    auto screenshot_path = tmp_dir.Path() / "my_screenshot.png";

    auto result = (*registry)(
            std::string("screenrecord screenshot --display 1 ") + screenshot_path.generic_string(),
            ctx);
    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");

    // Verify file was created
    EXPECT_TRUE(std::filesystem::exists(screenshot_path));
}

}  // namespace
}  // namespace goldfish::telnet
