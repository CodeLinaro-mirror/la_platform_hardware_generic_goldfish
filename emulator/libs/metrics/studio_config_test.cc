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

#include "android/base/testing/test_temp_dir.h"

namespace goldfish::metrics::studio {

namespace fs = std::filesystem;
using android::base::TestTempDir;

class StudioConfigTest : public ::testing::Test {
  protected:
    void SetUp() override { mTempDir = std::make_unique<TestTempDir>("studio_config_test"); }

    void TearDown() override { mTempDir.reset(); }

    void writeSettingsFile(const std::string& content) {
        std::ofstream os(temp_path() / "analytics.settings");
        os << content;
    }

    fs::path temp_path() const { return mTempDir->Path(); }

    std::unique_ptr<TestTempDir> mTempDir;
};

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

TEST_F(StudioConfigTest, GetMetricsUserId_NoFile) {
    EXPECT_TRUE(GetMetricsUserId(temp_path()).empty());
}

TEST_F(StudioConfigTest, GetMetricsUserId_EmptyFile) {
    writeSettingsFile("");
    EXPECT_TRUE(GetMetricsUserId(temp_path()).empty());
}

TEST_F(StudioConfigTest, GetMetricsUserId_InvalidJson) {
    writeSettingsFile("{ invalid json }");
    EXPECT_TRUE(GetMetricsUserId(temp_path()).empty());
}

TEST_F(StudioConfigTest, GetMetricsUserId_Valid) {
    writeSettingsFile(R"({"userId": "test-user-id"})");
    EXPECT_EQ(GetMetricsUserId(temp_path()), "test-user-id");
}

TEST_F(StudioConfigTest, GetMetricsUserId_Missing) {
    writeSettingsFile(R"({"hasOptedIn": true})");
    EXPECT_TRUE(GetMetricsUserId(temp_path()).empty());
}

TEST_F(StudioConfigTest, GetMetricsUserId_NotAString) {
    writeSettingsFile(R"({"userId": 12345})");
    EXPECT_TRUE(GetMetricsUserId(temp_path()).empty());
}

}  // namespace goldfish::metrics::studio
