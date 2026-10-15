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

#include <chrono>
#include <filesystem>
#include <fstream>

#include "google/protobuf/util/json_util.h"

#include "analytics_settings.pb.h"
#include "android/base/testing/test_system.h"
#include "android/base/testing/test_temp_dir.h"

namespace goldfish::metrics::studio {

namespace fs = std::filesystem;
using android::base::TestSystem;
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
    // Protobuf JSON parser fails on empty file
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

// Protobuf strict parsing disallows numbers/strings for bools.
TEST_F(StudioConfigTest, GetUserMetricsOptIn_OptedIn_Number_FailsInProto) {
    writeSettingsFile(R"({"hasOptedIn": 1})");
    EXPECT_EQ(GetUserMetricsOptIn(temp_path()), OptInState::kUnknown);
}

TEST_F(StudioConfigTest, GetUserMetricsOptIn_MissingKey_DefaultsToFalseProto3) {
    writeSettingsFile(R"({"userId": "some-uuid"})");
    // Proto3 default for bool is false
    EXPECT_EQ(GetUserMetricsOptIn(temp_path()), OptInState::kOptedOut);
}

TEST_F(StudioConfigTest, GetMetricsUserId_NoFile) {
    EXPECT_TRUE(GetMetricsUserId(temp_path()).empty());
}

TEST_F(StudioConfigTest, GetMetricsUserId_Valid) {
    writeSettingsFile(R"({"userId": "test-user-id"})");
    EXPECT_EQ(GetMetricsUserId(temp_path()), "test-user-id");
}

TEST_F(StudioConfigTest, EnsureAnalyticsSettings_CreatesNewFile) {
    auto settings_path = temp_path() / "analytics.settings";
    EXPECT_FALSE(fs::exists(settings_path));

    EnsureAnalyticsSettings(temp_path());

    EXPECT_TRUE(fs::exists(settings_path));
    EXPECT_FALSE(GetMetricsUserId(temp_path()).empty());

    std::string salt = GetAnonymizationSalt(temp_path());
    EXPECT_NE(salt, "UNKNOWN_SALT");
    EXPECT_FALSE(salt.empty());

    EXPECT_EQ(GetUserMetricsOptIn(temp_path()), OptInState::kOptedOut);
}

TEST_F(StudioConfigTest, EnsureAnalyticsSettings_FixesMissingSalt) {
    writeSettingsFile(R"({"userId": "test-user"})");

    EnsureAnalyticsSettings(temp_path());

    // UUID should be preserved, salt should be generated
    EXPECT_EQ(GetMetricsUserId(temp_path()), "test-user");
    EXPECT_NE(GetAnonymizationSalt(temp_path()), "UNKNOWN_SALT");
}

void WriteUpdatesXml(const fs::path& xml_path, std::string_view content) {
    fs::create_directories(xml_path.parent_path());
    std::ofstream os(xml_path);
    os << content;
}

TEST_F(StudioConfigTest, GetUpdateChannel_NoStudioDirReturnsUnknown) {
    EXPECT_EQ(GetUpdateChannel(fs::path{}), UpdateChannel::kUnknown);
    EXPECT_EQ(GetUpdateChannel(temp_path() / "nonexistent"), UpdateChannel::kUnknown);
}

TEST_F(StudioConfigTest, GetUpdateChannel_ParsesStudioDirUpdatesXml) {
    const fs::path prefs_dir = temp_path() / "custom_studio";

    WriteUpdatesXml(prefs_dir / "options" / "updates.xml", R"xml(
<application>
  <component name="UpdatesConfigurable">
    <option name="UPDATE_CHANNEL_TYPE" value="eap" />
  </component>
</application>
)xml");
    EXPECT_EQ(GetUpdateChannel(prefs_dir), UpdateChannel::kCanary);

    WriteUpdatesXml(prefs_dir / "options" / "updates.xml", R"xml(
<application>
  <component name="UpdatesConfigurable">
    <option name="UPDATE_CHANNEL_TYPE" value="release" />
  </component>
</application>
)xml");
    EXPECT_EQ(GetUpdateChannel(prefs_dir), UpdateChannel::kStable);

    WriteUpdatesXml(prefs_dir / "options" / "updates.xml", R"xml(
<application>
  <component name="UpdatesConfigurable">
    <option name="UPDATE_CHANNEL_TYPE" value="beta" />
  </component>
</application>
)xml");
    EXPECT_EQ(GetUpdateChannel(prefs_dir), UpdateChannel::kBeta);

    WriteUpdatesXml(prefs_dir / "options" / "updates.xml", R"xml(
<application>
  <component name="UpdatesConfigurable">
    <option name="UPDATE_CHANNEL_TYPE" value="milestone" />
  </component>
</application>
)xml");
    EXPECT_EQ(GetUpdateChannel(prefs_dir), UpdateChannel::kDev);

    // Missing UPDATE_CHANNEL_TYPE option in valid updates.xml defaults to Canary.
    WriteUpdatesXml(prefs_dir / "options" / "updates.xml", R"xml(
<application>
  <component name="UpdatesConfigurable">
    <option name="LAST_TIME_CHECKED" value="1791452188030" />
  </component>
</application>
)xml");
    EXPECT_EQ(GetUpdateChannel(prefs_dir), UpdateChannel::kCanary);

    // Unrecognized channel value returns kUnknown.
    WriteUpdatesXml(prefs_dir / "options" / "updates.xml", R"xml(
<application>
  <component name="UpdatesConfigurable">
    <option name="UPDATE_CHANNEL_TYPE" value="invalid_channel" />
  </component>
</application>
)xml");
    EXPECT_EQ(GetUpdateChannel(prefs_dir), UpdateChannel::kUnknown);

    // Malformed XML returns kUnknown.
    WriteUpdatesXml(prefs_dir / "options" / "updates.xml", "<application><unclosed>");
    EXPECT_EQ(GetUpdateChannel(prefs_dir), UpdateChannel::kUnknown);

    // Legacy config/options/updates.xml path is also supported.
    const fs::path legacy_dir = temp_path() / "legacy_studio";
    WriteUpdatesXml(legacy_dir / "config" / "options" / "updates.xml", R"xml(
<application>
  <component name="UpdatesConfigurable">
    <option name="UPDATE_CHANNEL_TYPE" value="beta" />
  </component>
</application>
)xml");
    EXPECT_EQ(GetUpdateChannel(legacy_dir), UpdateChannel::kBeta);
}

TEST_F(StudioConfigTest, GetUpdateChannel_ReadsFromAndroidStudioPreferencesEnv) {
    TestSystem test_sys("/tmp", temp_path() / "home", temp_path() / "appdata");
    const fs::path prefs_dir = temp_path() / "custom_studio";
    test_sys.EnvSet("ANDROID_STUDIO_PREFERENCES", prefs_dir.string());

    WriteUpdatesXml(prefs_dir / "options" / "updates.xml", R"xml(
<application>
  <component name="UpdatesConfigurable">
    <option name="UPDATE_CHANNEL_TYPE" value="beta" />
  </component>
</application>
)xml");
    EXPECT_EQ(GetUpdateChannel(), UpdateChannel::kBeta);
}

fs::path ModernGoogleConfigDir(const fs::path& home_dir, const fs::path& appdata_dir) {
#if defined(_WIN32)
    (void)home_dir;
    return appdata_dir / "Google";
#elif defined(__APPLE__)
    (void)appdata_dir;
    return home_dir / "Library" / "Application Support" / "Google";
#else
    (void)appdata_dir;
    return home_dir / ".config" / "Google";
#endif
}

TEST_F(StudioConfigTest, FindLatestAndroidStudioDir_SelectsLatestModernGoogleStudioDir) {
    const fs::path home_dir = temp_path() / "home";
    const fs::path appdata_dir = temp_path() / "appdata";
    TestSystem test_sys("/tmp", home_dir, appdata_dir);

    EXPECT_TRUE(FindLatestAndroidStudioDir().empty());

    const fs::path google_dir = ModernGoogleConfigDir(home_dir, appdata_dir);
    // Non-matching directory names should be ignored.
    fs::create_directories(google_dir / "AndroidStudioProjects" / "options");
    EXPECT_TRUE(FindLatestAndroidStudioDir().empty());

    // Older version (2025.3.4).
    const fs::path dir_2025_3_4 = google_dir / "AndroidStudioWithBlaze2025.3.4";
    fs::create_directories(dir_2025_3_4);
    EXPECT_EQ(FindLatestAndroidStudioDir(), dir_2025_3_4);

    // Preview of 2026.2.2 sorts higher than 2025.3.4.
    const fs::path dir_preview_2026_2_2 = google_dir / "AndroidStudioWithBlazePreview2026.2.2";
    fs::create_directories(dir_preview_2026_2_2);
    EXPECT_EQ(FindLatestAndroidStudioDir(), dir_preview_2026_2_2);

    // Release 2026.2.2 sorts higher than Preview 2026.2.2.
    const fs::path dir_release_2026_2_2 = google_dir / "AndroidStudioWithBlaze2026.2.2";
    fs::create_directories(dir_release_2026_2_2);
    EXPECT_EQ(FindLatestAndroidStudioDir(), dir_release_2026_2_2);
}

TEST_F(StudioConfigTest, FindLatestAndroidStudioDir_FallsBackToLegacyStudioDir) {
    const fs::path home_dir = temp_path() / "home";
    const fs::path appdata_dir = temp_path() / "appdata";
    TestSystem test_sys("/tmp", home_dir, appdata_dir);

    // A non-hidden ~/AndroidStudio directory in $HOME should not be treated as a legacy config dir.
    fs::create_directories(home_dir / "AndroidStudio" / "config" / "options");
    EXPECT_TRUE(FindLatestAndroidStudioDir().empty());

    const fs::path legacy_dir = home_dir / ".AndroidStudio3.6";
    fs::create_directories(legacy_dir / "config" / "options");
    EXPECT_EQ(FindLatestAndroidStudioDir(), legacy_dir);
}

}  // namespace goldfish::metrics::studio
