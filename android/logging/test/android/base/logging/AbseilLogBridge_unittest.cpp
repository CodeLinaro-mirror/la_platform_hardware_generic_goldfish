// Copyright 2023 The Android Open Source Project
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
#include "android/base/logging/AbseilLogBridge.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

#include "absl/log/globals.h"
#include "absl/log/log.h"
#include "absl/log/log_sink_registry.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "aemu/base/testing/TestUtils.h"
#include "host-common/logging.h"

namespace {

using ::testing::EndsWith;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::StartsWith;

class CaptureLogSink : public absl::LogSink {
  public:
    void Send(const absl::LogEntry& entry) override {
        char level = 'I';
        switch (entry.log_severity()) {
            case absl::LogSeverity::kInfo:
                level = 'I';
                break;
            case absl::LogSeverity::kError:
                level = 'E';
                break;
            case absl::LogSeverity::kWarning:
                level = 'W';
                break;

            case absl::LogSeverity::kFatal:
                level = 'F';
                break;
        }
        captured_log_ = absl::StrFormat("%c %s:%d - %s", level, entry.source_basename(),
                                        entry.source_line(), entry.text_message());
    }

    std::string captured_log_;
};

class AbseilLogCBridgeTest : public ::testing::Test {
  protected:
    void SetUp() override {
        // Add the CaptureLogSink
        log_sink_ = std::make_unique<CaptureLogSink>();
        absl::AddLogSink(log_sink_.get());
        absl::SetVLogLevel("*", 2);

        // Set log level to capture everything (adjust as needed)
        absl::SetStderrThreshold(absl::LogSeverity::kInfo);
    }

    void TearDown() override {
        // Remove the CaptureLogSink
        absl::RemoveLogSink(log_sink_.get());
    }

    // Common test parameters
    const char* format = "This is a %s message";

    std::unique_ptr<CaptureLogSink> log_sink_;
};

TEST_F(AbseilLogCBridgeTest, InfoLog) {
    ALOGI("This is a %s message", "INFO");
    auto line = __LINE__ - 1;
    EXPECT_EQ(log_sink_->captured_log_,
              absl::StrFormat("I AbseilLogBridge_unittest.cpp:%d - This is a INFO message", line));
}

TEST_F(AbseilLogCBridgeTest, ErrorLog) {
    ALOGE("This is a %s message", "ERROR");
    auto line = __LINE__ - 1;
    EXPECT_EQ(log_sink_->captured_log_,
              absl::StrFormat("E AbseilLogBridge_unittest.cpp:%d - This is a ERROR message", line));
}

TEST_F(AbseilLogCBridgeTest, WarningLog) {
    ALOGW("This is a %s message", "WARNING");
    auto line = __LINE__ - 1;
    EXPECT_EQ(
            log_sink_->captured_log_,
            absl::StrFormat("W AbseilLogBridge_unittest.cpp:%d - This is a WARNING message", line));
}

TEST_F(AbseilLogCBridgeTest, VLOG1) {
    ALOGV(1, "This is a %s message", "VLOG(1)");
    auto line = __LINE__ - 1;
    EXPECT_EQ(
            log_sink_->captured_log_,
            absl::StrFormat("I AbseilLogBridge_unittest.cpp:%d - This is a VLOG(1) message", line));
}

TEST_F(AbseilLogCBridgeTest, VLOG2) {
    ALOGV(2, "This is a %s message", "VLOG(2)");
    auto line = __LINE__ - 1;
    EXPECT_EQ(
            log_sink_->captured_log_,
            absl::StrFormat("I AbseilLogBridge_unittest.cpp:%d - This is a VLOG(2) message", line));
}

// Test truncation when message exceeds buffer size
TEST_F(AbseilLogCBridgeTest, Truncation) {
    std::string long_msg(4100, 'x');  // Exceeds buffer size
    ALOGI("%s", long_msg.c_str());
    std::string expected_msg = long_msg.substr(0, 4093) + "...";
    EXPECT_THAT(log_sink_->captured_log_, testing::HasSubstr(expected_msg));
}

}  // namespace
