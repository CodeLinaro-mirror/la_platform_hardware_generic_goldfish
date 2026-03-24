/* Copyright 2026 The Android Open Source Project
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

#include "goldfish/metrics/studio_config.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "android/base/testing/TestTempDir.h"

namespace goldfish::metrics::studio {

namespace fs = std::filesystem;
using android::base::TestTempDir;

class StudioConfigTest : public ::testing::Test {
  protected:
    void SetUp() override { mTempDir = std::make_unique<TestTempDir>("studio_config_test"); }

    void TearDown() override { mTempDir.reset(); }

    void writeSettingsFile(const std::string& content) {
        std::ofstream os(GetSettingsFilePath(temp_path()));
        os << content;
    }

    fs::path temp_path() const { return mTempDir->Path(); }

    std::unique_ptr<TestTempDir> mTempDir;
};

TEST_F(StudioConfigTest, GetSettingsFilePath) {
    fs::path expected = temp_path() / "analytics.settings";
    EXPECT_EQ(GetSettingsFilePath(temp_path()), expected);
}

TEST_F(StudioConfigTest, GetSpoolDirectory) {
    fs::path expected = temp_path() / "metrics" / "spool";
    EXPECT_EQ(GetSpoolDirectory(temp_path()), expected);
}

TEST_F(StudioConfigTest, GetUserMetricsOptIn_NoFile) {
    EXPECT_EQ(GetUserMetricsOptIn(temp_path()), OptInState::kUnknown);
}

TEST_F(StudioConfigTest, GetUserMetricsOptIn_EmptyFile) {
    writeSettingsFile("");
    EXPECT_EQ(GetUserMetricsOptIn(temp_path()), OptInState::kUnknown);
}

TEST_F(StudioConfigTest, GetUserMetricsOptIn_InvalidJson) {
    writeSettingsFile("{ invalid json }");
    EXPECT_EQ(GetUserMetricsOptIn(temp_path()), OptInState::kUnknown);
}

TEST_F(StudioConfigTest, GetUserMetricsOptIn_OptedIn_Bool) {
    writeSettingsFile(R"({"hasOptedIn": true})");
    EXPECT_EQ(GetUserMetricsOptIn(temp_path()), OptInState::kOptedIn);
}

TEST_F(StudioConfigTest, GetUserMetricsOptIn_OptedOut_Bool) {
    writeSettingsFile(R"({"hasOptedIn": false})");
    EXPECT_EQ(GetUserMetricsOptIn(temp_path()), OptInState::kOptedOut);
}

TEST_F(StudioConfigTest, GetUserMetricsOptIn_OptedIn_Number) {
    writeSettingsFile(R"({"hasOptedIn": 1})");
    EXPECT_EQ(GetUserMetricsOptIn(temp_path()), OptInState::kOptedIn);
}

TEST_F(StudioConfigTest, GetUserMetricsOptIn_OptedOut_Number) {
    writeSettingsFile(R"({"hasOptedIn": 0})");
    EXPECT_EQ(GetUserMetricsOptIn(temp_path()), OptInState::kOptedOut);
}

TEST_F(StudioConfigTest, GetUserMetricsOptIn_OptedIn_String) {
    writeSettingsFile(R"({"hasOptedIn": "true"})");
    EXPECT_EQ(GetUserMetricsOptIn(temp_path()), OptInState::kOptedIn);

    writeSettingsFile(R"({"hasOptedIn": "1"})");
    EXPECT_EQ(GetUserMetricsOptIn(temp_path()), OptInState::kOptedIn);
}

TEST_F(StudioConfigTest, GetUserMetricsOptIn_OptedOut_String) {
    writeSettingsFile(R"({"hasOptedIn": "false"})");
    EXPECT_EQ(GetUserMetricsOptIn(temp_path()), OptInState::kOptedOut);
}

TEST_F(StudioConfigTest, GetUserMetricsOptIn_MissingKey) {
    writeSettingsFile(R"({"userId": "some-uuid"})");
    EXPECT_EQ(GetUserMetricsOptIn(temp_path()), OptInState::kUnknown);
}

}  // namespace goldfish::metrics::studio
