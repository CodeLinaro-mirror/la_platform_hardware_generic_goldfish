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

#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"

#include "android/base/testing/test_system.h"

using android::base::TestSystem;
using android::goldfish::ConfigDirs;
using ::testing::Not;

namespace fs = std::filesystem;

TEST(ConfigDirs, getUserDirectoryDefault) {
    TestSystem sys("bin", "myhome");
    fs::path kExpected = fs::path("myhome") / ".android";
    EXPECT_THAT(ConfigDirs::GetUserDirectory(), testing::Eq(kExpected));
}

TEST(ConfigDirs, getUserDirectoryWithAndroidSdkHome) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("ANDROID_SDK_HOME", (sys.GetTempRoot()->Path() / "android-sdk").string());
    EXPECT_THAT(ConfigDirs::GetUserDirectory().string(), testing::EndsWith("android-sdk"));

    sys.GetTempRoot()->MakeSubDir(fs::path("android-sdk"));
    sys.GetTempRoot()->MakeSubDir(fs::path("android-sdk") / ".android");
    EXPECT_THAT(ConfigDirs::GetUserDirectory().string(),
                testing::EndsWith(fs::path("android-sdk/.android").make_preferred().string()));
}

TEST(ConfigDirs, getUserDirectoryWithAndroidSdkHomeAndPrefsRoot) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("ANDROID_SDK_HOME", (sys.GetTempRoot()->Path() / "android-sdk").string());
    sys.EnvSet("ANDROID_SDK_HOME", (sys.GetTempRoot()->Path() / "android-sdk-new").string());
    EXPECT_THAT(ConfigDirs::GetUserDirectory().string(), testing::EndsWith("android-sdk-new"));

    sys.GetTempRoot()->MakeSubDir(fs::path("android-sdk-new"));
    sys.GetTempRoot()->MakeSubDir(fs::path("android-sdk-new") / ".android");
    EXPECT_THAT(ConfigDirs::GetUserDirectory().string(),
                testing::EndsWith(fs::path("android-sdk-new/.android").make_preferred().string()));
}

TEST(ConfigDirs, getUserDirectoryWithAndroidEmulatorHome) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("ANDROID_EMULATOR_HOME",
               "android"
               "home");
    EXPECT_THAT(ConfigDirs::GetUserDirectory().string(), testing::EndsWith("androidhome"));
}

TEST(ConfigDirs, getSdkRootDirectory) {
    TestSystem sys("", "myhome");
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Sdk")));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Sdk") / "platform-tools"));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Sdk") / "platforms"));

    fs::path launcher_dir = sys.GetTempRoot()->Path();

    sys.EnvSet("ANDROID_SDK_ROOT", (sys.GetTempRoot()->Path() / "Sdk").string());
    EXPECT_THAT(ConfigDirs::GetSdkRootDirectory(launcher_dir, true).string(),
                testing::EndsWith("Sdk"));

    sys.EnvSet("ANDROID_SDK_ROOT",
               absl::StrCat("\"", (sys.GetTempRoot()->Path() / "Sdk").string(), "\""));
    EXPECT_THAT(ConfigDirs::GetSdkRootDirectory(launcher_dir, true).string(),
                testing::EndsWith("Sdk"));

    sys.EnvSet("ANDROID_SDK_ROOT", "");
    EXPECT_THAT(ConfigDirs::GetSdkRootDirectory(launcher_dir, true).string(),
                Not(testing::EndsWith("Sdk")));

    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Sdk2")));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Sdk2") / "platform-tools"));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Sdk2") / "platforms"));

    // ANDROID_HOME should take precedence over ANDROID_SDK_ROOT
    sys.EnvSet("ANDROID_HOME", (sys.GetTempRoot()->Path() / "Sdk2").string());
    EXPECT_THAT(ConfigDirs::GetSdkRootDirectory(launcher_dir, true).string(),
                testing::EndsWith("Sdk2"));

    // Bad ANDROID_HOME falls back to ANDROID_SDK_ROOT
    sys.EnvSet("ANDROID_HOME", (sys.GetTempRoot()->Path() / "bogus").string());
    sys.EnvSet("ANDROID_SDK_ROOT", (sys.GetTempRoot()->Path() / "Sdk").string());
    EXPECT_THAT(ConfigDirs::GetSdkRootDirectory(launcher_dir, true).string(),
                testing::EndsWith("Sdk"));
}

TEST(ConfigDirs, getAvdRootDirectory) {
    TestSystem sys("", "myhome");
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Area_1")));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Area_1") / ".android"));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Area_1") / ".android" / "avd"));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Area_2")));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Area_2") / ".android"));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Area_2") / ".android" / "avd"));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Area_3")));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Area_3") / ".android"));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Area_3") / ".android" / "avd"));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Area_4")));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Area_4") / ".android"));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Area_4") / ".android" / "avd"));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Area_5")));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Area_5") / ".android"));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Area_5") / ".android" / "avd"));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Area_6")));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Area_6") / ".android"));
    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("Area_6") / ".android" / "avd"));

    // Order of precedence is
    //   ANDROID_AVD_HOME
    //   ANDROID_PREFS_ROOT
    //   ANDROID_SDK_HOME
    //   TEMP_TSTDIR
    //   USER_HOME or HOME
    //   ANDROID_EMULATOR_HOME

    sys.EnvSet("ANDROID_AVD_HOME",
               (sys.GetTempRoot()->Path() / "Area_1/.android/avd").make_preferred().string());
    sys.EnvSet("ANDROID_PREFS_ROOT", (sys.GetTempRoot()->Path() / "Area_2").string());
    sys.EnvSet("ANDROID_SDK_HOME", (sys.GetTempRoot()->Path() / "Area_3").string());
    sys.EnvSet("TEST_TMPDIR", (sys.GetTempRoot()->Path() / "Area_4").string());
    sys.EnvSet("USER_HOME", (sys.GetTempRoot()->Path() / "Area_5").string());
    sys.EnvSet("ANDROID_EMULATOR_HOME",
               (sys.GetTempRoot()->Path() / "Area_6/.android").make_preferred().string());
    EXPECT_THAT(ConfigDirs::GetAvdRootDirectory().string(),
                testing::EndsWith(fs::path("Area_1/.android/avd").make_preferred().string()));

    sys.EnvSet("ANDROID_AVD_HOME", (sys.GetTempRoot()->Path() / "bogus").string());
    EXPECT_THAT(ConfigDirs::GetAvdRootDirectory().string(),
                testing::EndsWith(fs::path("Area_2/.android/avd").make_preferred().string()));

    sys.EnvSet("ANDROID_PREFS_ROOT", (sys.GetTempRoot()->Path() / "bogus").string());
    EXPECT_THAT(ConfigDirs::GetAvdRootDirectory().string(),
                testing::EndsWith(fs::path("Area_3/.android/avd").make_preferred().string()));

    sys.EnvSet("ANDROID_SDK_HOME", (sys.GetTempRoot()->Path() / "bogus").string());
    EXPECT_THAT(ConfigDirs::GetAvdRootDirectory().string(),
                testing::EndsWith(fs::path("Area_4/.android/avd").make_preferred().string()));

    sys.EnvSet("TEST_TMPDIR", (sys.GetTempRoot()->Path() / "bogus").string());
    EXPECT_THAT(ConfigDirs::GetAvdRootDirectory().string(),
                testing::EndsWith(fs::path("Area_5/.android/avd").make_preferred().string()));

    sys.EnvSet("USER_HOME", (sys.GetTempRoot()->Path() / "bogus").string());
    EXPECT_THAT(ConfigDirs::GetAvdRootDirectory().string(),
                testing::EndsWith(fs::path("Area_6/.android/avd").make_preferred().string()));
}
