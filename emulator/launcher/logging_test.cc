// Copyright 2024 The Android Open Source Project
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

#include "logging.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "absl/log/globals.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

#include "android/base/system.h"
#include "android/cmdline_definitions.h"
#include "android/cmdline_option.h"

namespace {

// Helper to reset absl log state between tests (if possible)
void ResetAbslLogState() {
    absl::SetMinLogLevel(absl::LogSeverityAtLeast::kWarning);
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kWarning);
    absl::SetVLogLevel("*", 0);  // Reset all vmodule levels
}

class ConfigureLoggingRealTest : public ::testing::Test {
  protected:
    void SetUp() override { 
        android::base::System::Get()->SetEnvironmentVariable("AEMU_NO_LOG_SINK", "TRUE");
        ResetAbslLogState();
    }

    void TearDown() override { ResetAbslLogState(); }
};

TEST_F(ConfigureLoggingRealTest, SetsInfoLevelWhenVerbose) {
    AndroidOptions opts = {};
    opts.verbose = 1;
    opts.vmodule = nullptr;
    configureLogging(opts);
    EXPECT_EQ(absl::MinLogLevel(), absl::LogSeverityAtLeast::kInfo);
    EXPECT_EQ(absl::StderrThreshold(), absl::LogSeverityAtLeast::kInfo);
}

TEST_F(ConfigureLoggingRealTest, SetsWarningLevelWhenNotVerbose) {
    AndroidOptions opts = {};
    opts.verbose = 0;
    opts.vmodule = nullptr;
    configureLogging(opts);
    EXPECT_EQ(absl::MinLogLevel(), absl::LogSeverityAtLeast::kWarning);
    EXPECT_EQ(absl::StderrThreshold(), absl::LogSeverityAtLeast::kInfo);
}

TEST_F(ConfigureLoggingRealTest, IgnoresNullVmodule) {
    AndroidOptions opts = {};
    opts.verbose = 1;
    opts.vmodule = nullptr;
    std::vector<std::pair<std::string, int>> seen;
    configureLogging(opts, [&](std::string_view glob, int level) {
        seen.emplace_back(std::string(glob), level);
    });
    EXPECT_TRUE(seen.empty());
}

TEST_F(ConfigureLoggingRealTest, IgnoresEmptyVmodule) {
    AndroidOptions opts = {};
    opts.verbose = 1;
    opts.vmodule = (char*)"";
    std::vector<std::pair<std::string, int>> seen;
    configureLogging(opts, [&](std::string_view glob, int level) {
        seen.emplace_back(std::string(glob), level);
    });
    EXPECT_TRUE(seen.empty());
}

TEST_F(ConfigureLoggingRealTest, SetsSingleVmoduleLevel) {
    AndroidOptions opts = {};
    opts.verbose = 1;
    opts.vmodule = (char*)"foo=2";
    std::vector<std::pair<std::string, int>> seen;
    configureLogging(opts, [&](std::string_view glob, int level) {
        seen.emplace_back(std::string(glob), level);
    });
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_EQ(seen[0].first, "foo");
    EXPECT_EQ(seen[0].second, 2);
}

TEST_F(ConfigureLoggingRealTest, IgnoresInvalidVmodulePattern) {
    AndroidOptions opts = {};
    opts.verbose = 1;
    opts.vmodule = (char*)"foo";  // No '='
    std::vector<std::pair<std::string, int>> seen;
    configureLogging(opts, [&](std::string_view glob, int level) {
        seen.emplace_back(std::string(glob), level);
    });
    EXPECT_TRUE(seen.empty());
}

TEST_F(ConfigureLoggingRealTest, IgnoresNonIntegerLevel) {
    AndroidOptions opts = {};
    opts.verbose = 1;
    opts.vmodule = (char*)"foo=bar";
    std::vector<std::pair<std::string, int>> seen;
    configureLogging(opts, [&](std::string_view glob, int level) {
        seen.emplace_back(std::string(glob), level);
    });
    EXPECT_TRUE(seen.empty());
}

TEST_F(ConfigureLoggingRealTest, HandlesMultiplePatterns) {
    AndroidOptions opts = {};
    opts.verbose = 1;
    opts.vmodule = (char*)"foo=2,bar=3";
    std::vector<std::pair<std::string, int>> seen;
    configureLogging(opts, [&](std::string_view glob, int level) {
        seen.emplace_back(std::string(glob), level);
    });
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0].first, "foo");
    EXPECT_EQ(seen[0].second, 2);
    EXPECT_EQ(seen[1].first, "bar");
    EXPECT_EQ(seen[1].second, 3);
}

}  // namespace
