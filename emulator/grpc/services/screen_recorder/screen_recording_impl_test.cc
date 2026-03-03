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
#include <string>

#include "gtest/gtest.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/display/test/fake_multi_display.h"

namespace android::emulation::control::incubating {

using ::goldfish::async::LibuvEventLoop;
using ::goldfish::display::test::FakeMultiDisplay;

class ScreenRecordingServiceImplTest : public ::testing::Test {
  protected:
    void SetUp() override {
        loop = LibuvEventLoop::Create();
        fake_display = std::make_unique<FakeMultiDisplay>(loop.get());
        // Create default display 0
        fake_display->CreateDisplay(0, 1080, 2400, 440, 0);
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
    EXPECT_EQ(response.fps(), 24);
    EXPECT_EQ(response.bit_rate(), 2000000);
    EXPECT_EQ(response.time_limit(), 180);
    EXPECT_EQ(response.state(), RecordingInfo::RECORDER_STATE_RECORDING);

    service->StopRecording(&context, &request, &response);
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

}  // namespace android::emulation::control::incubating
