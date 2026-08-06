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

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "gtest/gtest.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/display/test/fake_multi_display.h"
#include "goldfish/display/test/fake_pixman_display.h"

// Undefine close macro on Windows to avoid conflict with std::ofstream::close()
#ifdef _WIN32
#undef close
#endif

namespace android::emulation::control::incubating {

using ::goldfish::async::LibuvEventLoop;
using ::goldfish::display::test::FakeMultiDisplay;

class ScreenRecordingServiceImplTest : public ::testing::Test {
  protected:
    void SetUp() override {
        loop = LibuvEventLoop::Create();
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

    std::unique_ptr<LibuvEventLoop> loop;
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

    // Allow recorder thread to generate and encode several frames
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::filesystem::exists(test_file) && std::filesystem::file_size(test_file) > 1000) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

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
    std::string test_file = (temp_dir_ / "test_invalid.mp4").string();

    RecordingInfo request;
    request.set_file_name(test_file);

    RecordingInfo response;
    grpc::ServerContext context;

    auto status = service->StartRecording(&context, &request, &response);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_F(ScreenRecordingServiceImplTest, StartRecordingFailsForInvalidFps) {
    std::string test_file = (temp_dir_ / "test_invalid_fps.webm").string();

    RecordingInfo request;
    request.set_file_name(test_file);
    request.set_fps(100);  // Max is 60

    RecordingInfo response;
    grpc::ServerContext context;

    auto status = service->StartRecording(&context, &request, &response);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_F(ScreenRecordingServiceImplTest, StartRecordingFailsIfFileExists) {
    std::string test_file = (temp_dir_ / "test_exists.webm").string();
    // Create the file first
    std::ofstream file(test_file);
    file << "dummy content";
    file.close();

    RecordingInfo request;
    request.set_file_name(test_file);

    RecordingInfo response;
    grpc::ServerContext context;

    auto status = service->StartRecording(&context, &request, &response);

    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error_code(), grpc::StatusCode::ALREADY_EXISTS);

    // Clean up
    std::remove(test_file.c_str());
}

}  // namespace android::emulation::control::incubating
