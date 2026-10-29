// Copyright 2026 The Android Open Source Project
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

#include "android/emulation/control/incubating/screen_recording_impl.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/display/test/fake_multi_display.h"
#include "goldfish/display/test/fake_pixman_display.h"
#include "goldfish/file/file.h"

// Undefine close macro on Windows to avoid conflict with std::ofstream::close()
#ifdef _WIN32
#undef close
#endif

namespace android::emulation::control::incubating {

using ::goldfish::async::LibuvEventLoop;
using ::goldfish::async::ThreadedEventLoop;
using ::goldfish::display::test::FakeMultiDisplay;
using ::testing::AllOf;
using ::testing::HasSubstr;

namespace {

class ExpiredDisplayMultiDisplay : public FakeMultiDisplay {
  public:
    explicit ExpiredDisplayMultiDisplay(::goldfish::async::EventLoop* event_loop)
            : FakeMultiDisplay(event_loop) {}

    absl::StatusOr<::goldfish::display::DisplayPtr> GetDisplay(
            ::goldfish::display::DisplayId /*display_id*/) const override {
        return ::goldfish::display::DisplayPtr{};
    }
};

}  // namespace

class ScreenRecordingServiceImplTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // The display posts demand changes and captures to its loop, so the
        // loop must run for the recorder to receive frames.
        loop = ThreadedEventLoop::Create(LibuvEventLoop::Create());
        fake_display = std::make_unique<FakeMultiDisplay>(loop.get());
        // Create default display 0
        fake_display->CreateDisplay(0, 1080, 2400, 440, 0).IgnoreError();
        service = std::make_unique<ScreenRecordingServiceImpl>(fake_display.get());

        // Setup platform-independent temp path
        temp_dir_ = std::filesystem::temp_directory_path();
        if (const char* bazel_tmp = std::getenv("TEST_TMPDIR")) {
            temp_dir_ = bazel_tmp;
        }
    }

    std::unique_ptr<::goldfish::async::EventLoop> loop;
    std::unique_ptr<FakeMultiDisplay> fake_display;
    std::unique_ptr<ScreenRecordingServiceImpl> service;
    std::filesystem::path temp_dir_;
};

TEST_F(ScreenRecordingServiceImplTest, StartRecordingAppliesDefaults) {
    std::string test_file = (temp_dir_ / "test_defaults.webm").string();

    RecordingInfo request;
    request.set_file_name(test_file);
    request.set_fps(0);       // Request default
    request.set_bit_rate(0);  // Request default

    RecordingInfo response;
    grpc::ServerContext context;

    auto status = service->StartRecording(&context, &request, &response);

    EXPECT_TRUE(status.ok()) << status.error_message();
    EXPECT_EQ(response.fps(), kFPS);
    EXPECT_EQ(response.bit_rate(), kDefaultVideoBitrate);
    EXPECT_EQ(response.time_limit(), kDefaultTimeLimit);
    EXPECT_EQ(response.state(), RecordingInfo::RECORDER_STATE_RECORDING);

    service->StopRecording(&context, &request, &response);
    std::remove(test_file.c_str());
}

TEST_F(ScreenRecordingServiceImplTest, StartRecordingWithFoldableResolution) {
    // Create display 1 with resolution 1260x2400 (where 1260 / 2 = 630, which is odd)
    fake_display->CreateDisplay(1, 1260, 2400, 440, 0).IgnoreError();

    std::string test_file = (temp_dir_ / "test_foldable.webm").string();

    RecordingInfo request;
    request.set_file_name(test_file);
    request.set_display(1);
    request.set_fps(24);
    request.set_bit_rate(2000000);

    RecordingInfo response;
    grpc::ServerContext context;

    auto status = service->StartRecording(&context, &request, &response);

    EXPECT_TRUE(status.ok()) << status.error_message();

    // StopRecording drops the frames the encoder has not reached yet, and under
    // sanitizers one frame can take seconds to encode, so wait for encoded
    // frames rather than for a fixed time. Eleven frames of this static screen
    // encode to about 1.7 KB: two keyframes and nine small delta frames.
    constexpr uint64_t kMinEncodedFrames = 11;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (service->FramesEncodedForTesting() < kMinEncodedFrames &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_GE(service->FramesEncodedForTesting(), kMinEncodedFrames);

    service->StopRecording(&context, &request, &response);
    EXPECT_GT(std::filesystem::file_size(test_file), 1000)
            << "Recording output should contain valid WebM video frames and header";
    std::remove(test_file.c_str());
}

TEST_F(ScreenRecordingServiceImplTest, DISABLED_StartRecording5Seconds) {
    std::string test_file = (temp_dir_ / "test_5s.webm").string();

    // Start the fake display generator so it produces varying frames (changing colors)
    auto display_ptr = fake_display->GetDisplay(0).value().lock();
    auto active_display =
            std::static_pointer_cast<::goldfish::display::test::ActiveFakePixmanDisplay>(
                    display_ptr);
    ASSERT_TRUE(active_display != nullptr);
    active_display->Start();

    RecordingInfo request;
    request.set_file_name(test_file);
    request.set_fps(24);
    request.set_bit_rate(4000000);  // 4Mbps

    RecordingInfo response;
    grpc::ServerContext context;

    auto status = service->StartRecording(&context, &request, &response);
    EXPECT_TRUE(status.ok()) << status.error_message();

    // Record for 5 seconds
    std::this_thread::sleep_for(std::chrono::seconds(5));

    status = service->StopRecording(&context, &request, &response);
    EXPECT_TRUE(status.ok()) << status.error_message();

    active_display->Stop();

    EXPECT_TRUE(std::filesystem::exists(test_file));
    uint64_t size = std::filesystem::file_size(test_file);
    EXPECT_GT(size, 10000) << "File size is too small: " << size << " bytes";

    // We can also print the size for verification
    std::cout << "[Test] 5s Recording file size with dynamic content: " << size << " bytes"
              << std::endl;

    std::remove(test_file.c_str());
}

TEST_F(ScreenRecordingServiceImplTest, StartRecordingFailsForInvalidFileName) {
    const std::string test_file = (temp_dir_ / "vidéo_中文.mp4").string();

    RecordingInfo request;
    request.set_file_name(test_file);

    RecordingInfo response;
    grpc::ServerContext context;

    auto status = service->StartRecording(&context, &request, &response);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
    EXPECT_THAT(status.error_message(),
                AllOf(HasSubstr("Failed to start screen recording"), HasSubstr("vidéo_中文.mp4"),
                      HasSubstr("must end with '.webm'"),
                      HasSubstr("Specify a destination path with a '.webm' extension.")));
}

TEST_F(ScreenRecordingServiceImplTest, StartRecordingFailsForInvalidFps) {
    const std::string test_file = (temp_dir_ / "test_invalid_fps.webm").string();

    RecordingInfo request;
    request.set_file_name(test_file);
    request.set_fps(100);  // Max is 60

    RecordingInfo response;
    grpc::ServerContext context;

    auto status = service->StartRecording(&context, &request, &response);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
    EXPECT_THAT(status.error_message(),
                AllOf(HasSubstr("test_invalid_fps.webm"),
                      HasSubstr(absl::StrCat("FPS 100 is out of range [1, ", kMaxFPS, "]")),
                      HasSubstr("Specify an FPS between 1 and")));
}

TEST_F(ScreenRecordingServiceImplTest, StartRecordingFailsForInvalidBitRate) {
    const std::string test_file = (temp_dir_ / "test_invalid_bitrate.webm").string();

    RecordingInfo request;
    request.set_file_name(test_file);
    request.set_bit_rate(kMinVideoBitrate - 1);

    RecordingInfo response;
    grpc::ServerContext context;

    auto status = service->StartRecording(&context, &request, &response);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
    EXPECT_THAT(
            status.error_message(),
            AllOf(HasSubstr("test_invalid_bitrate.webm"),
                  HasSubstr(absl::StrCat("bitrate ", kMinVideoBitrate - 1, " bps is out of range [",
                                         kMinVideoBitrate, ", ", kMaxVideoBitrate, "]")),
                  HasSubstr("Specify a bitrate between")));
}

TEST_F(ScreenRecordingServiceImplTest, StartRecordingFailsForExcessiveTimeLimit) {
    const std::string test_file = (temp_dir_ / "test_invalid_timelimit.webm").string();

    RecordingInfo request;
    request.set_file_name(test_file);
    request.set_time_limit(android::emulation::control::kMaxTimeLimit + 1);

    RecordingInfo response;
    grpc::ServerContext context;

    auto status = service->StartRecording(&context, &request, &response);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
    EXPECT_THAT(status.error_message(),
                AllOf(HasSubstr("test_invalid_timelimit.webm"),
                      HasSubstr(absl::StrCat("time limit ",
                                             android::emulation::control::kMaxTimeLimit + 1,
                                             "s is out of range [1, ",
                                             android::emulation::control::kMaxTimeLimit, "]")),
                      HasSubstr("Specify a time limit between 1 and")));
}

TEST_F(ScreenRecordingServiceImplTest, StartRecordingFailsWhenAlreadyInProgress) {
    const std::string first_file = (temp_dir_ / "active_first.webm").string();
    const std::string second_file = (temp_dir_ / "second_attempt.webm").string();

    RecordingInfo first_request;
    first_request.set_file_name(first_file);
    RecordingInfo first_response;
    grpc::ServerContext context1;

    ASSERT_TRUE(service->StartRecording(&context1, &first_request, &first_response).ok());

    RecordingInfo second_request;
    second_request.set_file_name(second_file);
    RecordingInfo second_response;
    grpc::ServerContext context2;

    auto status = service->StartRecording(&context2, &second_request, &second_response);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::ALREADY_EXISTS);
    EXPECT_THAT(status.error_message(), AllOf(HasSubstr("second_attempt.webm"),
                                              HasSubstr("screen recording is already in progress"),
                                              HasSubstr("Call StopRecording")));

    grpc::ServerContext stop_context;
    EXPECT_TRUE(service->StopRecording(&stop_context, &first_request, &first_response).ok());
    android::base::file::rm(first_file).IgnoreError();
}

TEST_F(ScreenRecordingServiceImplTest, StartRecordingFailsForMissingAndExpiredDisplay) {
    const std::string test_file = (temp_dir_ / "test_missing_display.webm").string();

    RecordingInfo request;
    request.set_file_name(test_file);
    request.set_display(99);

    RecordingInfo response;
    grpc::ServerContext context;

    auto status = service->StartRecording(&context, &request, &response);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::NOT_FOUND);
    EXPECT_THAT(status.error_message(),
                AllOf(HasSubstr("test_missing_display.webm"), HasSubstr("on display 99"),
                      HasSubstr("Display with id 99 not found"),
                      HasSubstr("Verify that display 99 exists and is active")));

    ExpiredDisplayMultiDisplay expired_display(loop.get());
    ScreenRecordingServiceImpl expired_service(&expired_display);
    request.set_display(7);
    grpc::ServerContext expired_context;
    status = expired_service.StartRecording(&expired_context, &request, &response);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::UNAVAILABLE);
    EXPECT_THAT(status.error_message(),
                AllOf(HasSubstr("test_missing_display.webm"), HasSubstr("on display 7"),
                      HasSubstr("display 7 is no longer active"),
                      HasSubstr("Verify that display 7 has not been removed")));
}

TEST_F(ScreenRecordingServiceImplTest, StartRecordingFailsWhenExistingPathCannotBeRemoved) {
    const std::filesystem::path conflict_dir = temp_dir_ / "locked_dir.webm";
    ASSERT_TRUE(android::base::file::mkdir_recursive(conflict_dir, 0700).ok());
    ASSERT_TRUE(android::base::file::touch(conflict_dir / "keep").ok());

    RecordingInfo request;
    request.set_file_name(conflict_dir.string());
    RecordingInfo response;
    grpc::ServerContext context;

    auto status = service->StartRecording(&context, &request, &response);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::INTERNAL);
    EXPECT_THAT(status.error_message(),
                AllOf(HasSubstr("unable to remove existing recording file"),
                      HasSubstr("locked_dir.webm"), HasSubstr("write permissions")));

    android::base::file::rm_recursive(conflict_dir).IgnoreError();
}

TEST_F(ScreenRecordingServiceImplTest, StartRecordingFailsForMissingParentDirWithUnicodePath) {
    const std::string test_file = (temp_dir_ / "täst_中文_missing" / "vidéo.webm").string();

    RecordingInfo request;
    request.set_file_name(test_file);
    RecordingInfo response;
    grpc::ServerContext context;

    auto status = service->StartRecording(&context, &request, &response);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::INTERNAL);
    EXPECT_THAT(status.error_message(),
                AllOf(HasSubstr("unable to create recording file"), HasSubstr("täst_中文_missing"),
                      HasSubstr("vidéo.webm"), HasSubstr("Ensure the parent directory exists")));
}

TEST_F(ScreenRecordingServiceImplTest, StartRecordingFailsWhenEncoderInitFails) {
    ASSERT_TRUE(fake_display->CreateDisplay(2, 1, 1, 440, 0).ok());
    const std::string test_file = (temp_dir_ / "test_encoder_fail.webm").string();

    RecordingInfo request;
    request.set_file_name(test_file);
    request.set_display(2);
    RecordingInfo response;
    grpc::ServerContext context;

    auto status = service->StartRecording(&context, &request, &response);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::INTERNAL);
    EXPECT_THAT(status.error_message(),
                AllOf(HasSubstr("test_encoder_fail.webm"), HasSubstr("on display 2"),
                      HasSubstr("unable to initialize WebM video encoder for 1x1"),
                      HasSubstr("check emulator logs")));

    android::base::file::rm(test_file).IgnoreError();
}

TEST_F(ScreenRecordingServiceImplTest, StopRecordingReportsActionableNotFoundErrors) {
    const std::string inactive_file = (temp_dir_ / "inactive_vidéo.webm").string();
    RecordingInfo stop_request;
    stop_request.set_file_name(inactive_file);
    RecordingInfo response;
    grpc::ServerContext context1;

    auto status = service->StopRecording(&context1, &stop_request, &response);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::NOT_FOUND);
    EXPECT_THAT(status.error_message(), AllOf(HasSubstr("inactive_vidéo.webm"),
                                              HasSubstr("no screen recording is currently active"),
                                              HasSubstr("Call StartRecording")));

    const std::string active_file = (temp_dir_ / "active_recording.webm").string();
    RecordingInfo start_request;
    start_request.set_file_name(active_file);
    grpc::ServerContext start_context;
    ASSERT_TRUE(service->StartRecording(&start_context, &start_request, &response).ok());

    const std::string wrong_file = (temp_dir_ / "wrong_target.webm").string();
    stop_request.set_file_name(wrong_file);
    grpc::ServerContext wrong_context;
    status = service->StopRecording(&wrong_context, &stop_request, &response);
    EXPECT_EQ(status.error_code(), grpc::StatusCode::NOT_FOUND);
    EXPECT_THAT(status.error_message(),
                AllOf(HasSubstr("no recording found for"), HasSubstr("wrong_target.webm"),
                      HasSubstr("ListRecordings")));

    grpc::ServerContext valid_stop_context;
    EXPECT_TRUE(service->StopRecording(&valid_stop_context, &start_request, &response).ok());
    android::base::file::rm(active_file).IgnoreError();
}

TEST_F(ScreenRecordingServiceImplTest, StartRecordingOverwritesExistingFile) {
    std::string test_file = (temp_dir_ / "test_overwrite.webm").string();
    // Create pre-existing file on disk
    std::ofstream file(test_file);
    file << "dummy existing content";
    file.close();

    RecordingInfo request;
    request.set_file_name(test_file);

    RecordingInfo response;
    grpc::ServerContext context;

    auto status = service->StartRecording(&context, &request, &response);
    EXPECT_TRUE(status.ok()) << status.error_message();

    service->StopRecording(&context, &request, &response);
    // Clean up
    std::remove(test_file.c_str());
}

TEST_F(ScreenRecordingServiceImplTest, StartRecordingSucceedsOnSecondAttempt) {
    std::string test_file = (temp_dir_ / "test_second_attempt.webm").string();

    RecordingInfo request;
    request.set_file_name(test_file);

    RecordingInfo response;
    grpc::ServerContext context1;

    // First recording attempt
    auto status = service->StartRecording(&context1, &request, &response);
    EXPECT_TRUE(status.ok()) << status.error_message();

    status = service->StopRecording(&context1, &request, &response);
    EXPECT_TRUE(status.ok()) << status.error_message();
    EXPECT_TRUE(std::filesystem::exists(test_file));

    // Second recording attempt to the same file path (e.g. Studio / Extended Controls staging)
    grpc::ServerContext context2;
    status = service->StartRecording(&context2, &request, &response);
    EXPECT_TRUE(status.ok()) << status.error_message();

    status = service->StopRecording(&context2, &request, &response);
    EXPECT_TRUE(status.ok()) << status.error_message();

    // Clean up
    std::remove(test_file.c_str());
}

}  // namespace android::emulation::control::incubating
