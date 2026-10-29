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

#include "android/base/studio_log_sink.h"

#include <iostream>
#include <sstream>
#include <string>

#include "absl/log/globals.h"
#include "absl/log/log_sink_registry.h"
#include "android/base/color_log_sink.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace android::base {
namespace {

class StudioLogSinkTest : public ::testing::Test {
  protected:
    void SetUp() override {
        prev_min_level_ = absl::MinLogLevel();
        // Match default launcher configuration without -verbose (kWarning).
        absl::SetMinLogLevel(absl::LogSeverityAtLeast::kWarning);
        SetStudioLogStreamForTesting(&studio_stream_);
        absl::AddLogSink(&global_sink_);
    }

    void TearDown() override {
        absl::RemoveLogSink(&global_sink_);
        SetStudioLogStreamForTesting(&std::cout);
        absl::SetMinLogLevel(prev_min_level_);
    }

    absl::LogSeverityAtLeast prev_min_level_ = absl::LogSeverityAtLeast::kInfo;
    std::ostringstream studio_stream_;
    std::ostringstream global_stream_;
    ColorLogSink global_sink_{&global_stream_, /*use_color=*/false};
};

TEST_F(StudioLogSinkTest, UserInfoEmitsExactPrefixWithoutVerboseAndSkipsGlobalSinks) {
    USER_MESSAGE(INFO) << "Informational notice";
    EXPECT_EQ(studio_stream_.str(), "USER_INFO    | Informational notice\n");
    EXPECT_TRUE(global_stream_.str().empty());
}

TEST_F(StudioLogSinkTest, UserWarningEmitsExactPrefixAndSkipsGlobalSinks) {
    USER_MESSAGE(WARNING) << "Low memory warning";
    EXPECT_EQ(studio_stream_.str(), "USER_WARNING | Low memory warning\n");
    EXPECT_TRUE(global_stream_.str().empty());
}

TEST_F(StudioLogSinkTest, UserErrorEmitsExactPrefixAndSkipsGlobalSinks) {
    USER_MESSAGE(ERROR) << "Recoverable error";
    EXPECT_EQ(studio_stream_.str(), "USER_ERROR   | Recoverable error\n");
    EXPECT_TRUE(global_stream_.str().empty());
}

TEST_F(StudioLogSinkTest, WorksInsideUnbracedIfElse) {
    bool flag = true;
    if (flag)
        USER_MESSAGE(WARNING) << "Taken branch";
    else
        USER_MESSAGE(ERROR) << "Untaken branch";

    EXPECT_EQ(studio_stream_.str(), "USER_WARNING | Taken branch\n");
}

TEST_F(StudioLogSinkTest, OwnsConstructedLabelAfterTemporaryExpires) {
    std::ostringstream custom_stream;
    StudioLogSink custom_sink(&custom_stream, std::string("CUSTOM_LABEL"));
    ABSL_LOG(WARNING).ToSinkOnly(&custom_sink) << "Safe label lifetime";
    EXPECT_EQ(custom_stream.str(), "CUSTOM_LABEL | Safe label lifetime\n");
}

TEST_F(StudioLogSinkTest, ExitWithStudioFatalMessageExitsQuietlyWithCodeOne) {
    SetStudioLogStreamForTesting(&std::cerr);
    EXPECT_EXIT(ExitWithStudioFatalMessage("Insufficient disk space."),
                ::testing::ExitedWithCode(1),
                "FATAL        \\| Insufficient disk space\\.");
}

}  // namespace
}  // namespace android::base
