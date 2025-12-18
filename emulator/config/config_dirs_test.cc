// Copyright 2015 The Android Open Source Project
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
#include "android/goldfish/config_dirs.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/strings/str_cat.h"
#include "absl/status/status_matchers.h"

#include "android/system/test-headers/android/base/testing/TestSystem.h"

using android::base::TestSystem;
using android::goldfish::ConfigDirs;
using ::testing::Not;

namespace fs = std::filesystem;

TEST(ConfigDirs, getUserDirectoryDefault) {
    TestSystem sys("bin", "myhome");
    fs::path kExpected = fs::path("myhome") / ".android";
    EXPECT_THAT(ConfigDirs::getUserDirectory(), testing::Eq(kExpected));
}

TEST(ConfigDirs, getUserDirectoryWithAndroidSdkHome) {
    TestSystem sys("bin", "myhome");
    sys.envSet("ANDROID_SDK_HOME", (sys.getTempRoot()->path() / "android-sdk").string());
    EXPECT_THAT(ConfigDirs::getUserDirectory().string(), testing::EndsWith("android-sdk"));

    sys.getTempRoot()->makeSubDir(fs::path("android-sdk"));
    sys.getTempRoot()->makeSubDir(fs::path("android-sdk") / ".android");
    EXPECT_THAT(ConfigDirs::getUserDirectory().string(),
                testing::EndsWith(fs::path("android-sdk/.android").make_preferred().string()));
}

TEST(ConfigDirs, getUserDirectoryWithAndroidSdkHomeAndPrefsRoot) {
    TestSystem sys("bin", "myhome");
    sys.envSet("ANDROID_SDK_HOME", (sys.getTempRoot()->path() / "android-sdk").string());
    sys.envSet("ANDROID_SDK_HOME", (sys.getTempRoot()->path() / "android-sdk-new").string());
    EXPECT_THAT(ConfigDirs::getUserDirectory().string(), testing::EndsWith("android-sdk-new"));

    sys.getTempRoot()->makeSubDir(fs::path("android-sdk-new"));
    sys.getTempRoot()->makeSubDir(fs::path("android-sdk-new") / ".android");
    EXPECT_THAT(ConfigDirs::getUserDirectory().string(),
                testing::EndsWith(fs::path("android-sdk-new/.android").make_preferred().string()));
}

TEST(ConfigDirs, getUserDirectoryWithAndroidEmulatorHome) {
    TestSystem sys("bin", "myhome");
    sys.envSet("ANDROID_EMULATOR_HOME",
               "android"
               "home");
    EXPECT_THAT(ConfigDirs::getUserDirectory().string(), testing::EndsWith("androidhome"));
}

TEST(ConfigDirs, getSdkRootDirectory) {
    TestSystem sys("", "myhome");
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Sdk")));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Sdk") / "platform-tools"));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Sdk") / "platforms"));

    fs::path launcher_dir = sys.getTempRoot()->path();

    sys.envSet("ANDROID_SDK_ROOT", (sys.getTempRoot()->path() / "Sdk").string());
    EXPECT_THAT(ConfigDirs::getSdkRootDirectory(launcher_dir, true).string(),
                testing::EndsWith("Sdk"));

    sys.envSet("ANDROID_SDK_ROOT",
               absl::StrCat("\"", (sys.getTempRoot()->path() / "Sdk").string(), "\""));
    EXPECT_THAT(ConfigDirs::getSdkRootDirectory(launcher_dir, true).string(),
                testing::EndsWith("Sdk"));

    sys.envSet("ANDROID_SDK_ROOT", "");
    EXPECT_THAT(ConfigDirs::getSdkRootDirectory(launcher_dir, true).string(),
                Not(testing::EndsWith("Sdk")));

    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Sdk2")));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Sdk2") / "platform-tools"));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Sdk2") / "platforms"));

    // ANDROID_HOME should take precedence over ANDROID_SDK_ROOT
    sys.envSet("ANDROID_HOME", (sys.getTempRoot()->path() / "Sdk2").string());
    EXPECT_THAT(ConfigDirs::getSdkRootDirectory(launcher_dir, true).string(),
                testing::EndsWith("Sdk2"));

    // Bad ANDROID_HOME falls back to ANDROID_SDK_ROOT
    sys.envSet("ANDROID_HOME", (sys.getTempRoot()->path() / "bogus").string());
    sys.envSet("ANDROID_SDK_ROOT", (sys.getTempRoot()->path() / "Sdk").string());
    EXPECT_THAT(ConfigDirs::getSdkRootDirectory(launcher_dir, true).string(),
                testing::EndsWith("Sdk"));
}

TEST(ConfigDirs, getAvdRootDirectory) {
    TestSystem sys("", "myhome");
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Area_1")));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Area_1") / ".android"));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Area_1") / ".android" / "avd"));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Area_2")));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Area_2") / ".android"));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Area_2") / ".android" / "avd"));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Area_3")));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Area_3") / ".android"));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Area_3") / ".android" / "avd"));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Area_4")));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Area_4") / ".android"));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Area_4") / ".android" / "avd"));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Area_5")));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Area_5") / ".android"));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Area_5") / ".android" / "avd"));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Area_6")));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Area_6") / ".android"));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Area_6") / ".android" / "avd"));

    // Order of precedence is
    //   ANDROID_AVD_HOME
    //   ANDROID_PREFS_ROOT
    //   ANDROID_SDK_HOME
    //   TEMP_TSTDIR
    //   USER_HOME or HOME
    //   ANDROID_EMULATOR_HOME

    sys.envSet("ANDROID_AVD_HOME",
               (sys.getTempRoot()->path() / "Area_1/.android/avd").make_preferred().string());
    sys.envSet("ANDROID_PREFS_ROOT", (sys.getTempRoot()->path() / "Area_2").string());
    sys.envSet("ANDROID_SDK_HOME", (sys.getTempRoot()->path() / "Area_3").string());
    sys.envSet("TEST_TMPDIR", (sys.getTempRoot()->path() / "Area_4").string());
    sys.envSet("USER_HOME", (sys.getTempRoot()->path() / "Area_5").string());
    sys.envSet("ANDROID_EMULATOR_HOME",
               (sys.getTempRoot()->path() / "Area_6/.android").make_preferred().string());
    EXPECT_THAT(ConfigDirs::getAvdRootDirectory().string(),
                testing::EndsWith(fs::path("Area_1/.android/avd").make_preferred().string()));

    sys.envSet("ANDROID_AVD_HOME", (sys.getTempRoot()->path() / "bogus").string());
    EXPECT_THAT(ConfigDirs::getAvdRootDirectory().string(),
                testing::EndsWith(fs::path("Area_2/.android/avd").make_preferred().string()));

    sys.envSet("ANDROID_PREFS_ROOT", (sys.getTempRoot()->path() / "bogus").string());
    EXPECT_THAT(ConfigDirs::getAvdRootDirectory().string(),
                testing::EndsWith(fs::path("Area_3/.android/avd").make_preferred().string()));

    sys.envSet("ANDROID_SDK_HOME", (sys.getTempRoot()->path() / "bogus").string());
    EXPECT_THAT(ConfigDirs::getAvdRootDirectory().string(),
                testing::EndsWith(fs::path("Area_4/.android/avd").make_preferred().string()));

    sys.envSet("TEST_TMPDIR", (sys.getTempRoot()->path() / "bogus").string());
    EXPECT_THAT(ConfigDirs::getAvdRootDirectory().string(),
                testing::EndsWith(fs::path("Area_5/.android/avd").make_preferred().string()));

    sys.envSet("USER_HOME", (sys.getTempRoot()->path() / "bogus").string());
    EXPECT_THAT(ConfigDirs::getAvdRootDirectory().string(),
                testing::EndsWith(fs::path("Area_6/.android/avd").make_preferred().string()));
}

class ConfigDirsTest : public testing::TestWithParam<bool> {};

TEST_P(ConfigDirsTest, getDiscoveryDirectory) {
    TestSystem sys("", "myhome");

    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("runtime")));
    fs::path base = sys.getTempRoot()->path() / "runtime";

#if defined(_WIN32)
    base = base / "Temp";
#elif defined(__APPLE__)
    base = base / "Library" / "Caches" / "TemporaryItems";
#endif

    auto want = base / "avd" / "running";
    if (GetParam()) {
        ASSERT_TRUE(android::base::file::mkdir_recursive(sys.getTempRoot()->path() / want, 0755).ok())
                << "creating: " << want;
        // Make sure that unreadable dir can be fixed.
        android::base::file::chmod(want, 0055).IgnoreError();
    }

    sys.envSet("LOCALAPPDATA", (sys.getTempRoot()->path() / "runtime").string());
    sys.envSet("XDG_RUNTIME_DIR", (sys.getTempRoot()->path() / "runtime").string());
    sys.envSet("HOME", (sys.getTempRoot()->path() / "runtime").string());

    auto got = ConfigDirs::getDiscoveryDirectory();
    EXPECT_THAT(got.string(), testing::EndsWith(want.string()));
    EXPECT_TRUE(android::base::file::exists(got));
    // On Windows this will return 0777 (instead of 0755) as there's only one read-only bit.
    EXPECT_THAT(android::base::file::mode(got), absl_testing::IsOkAndHolds(::testing::Truly([] (unsigned mode) { return mode & 0700 == 0700; })));
}

INSTANTIATE_TEST_SUITE_P(DiscoveryDirectory, ConfigDirsTest, testing::Values(true, false));
