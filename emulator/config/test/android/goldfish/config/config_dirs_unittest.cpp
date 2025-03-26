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
#include "android/goldfish/config/config_dirs.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "aemu/base/testing/FileMatchers.h"
#include "android/base/testing/TestSystem.h"

using android::base::TestSystem;
using android::goldfish::ConfigDirs;
using ::testing::Not;
using ::testing::PathEq;

namespace fs = std::filesystem;

TEST(ConfigDirs, getUserDirectoryDefault) {
    TestSystem sys("bin", "myhome");
    fs::path kExpected = fs::path("myhome") / ".android";
    EXPECT_THAT(ConfigDirs::getUserDirectory(), PathEq(kExpected));
}

TEST(ConfigDirs, getUserDirectoryWithAndroidSdkHome) {
    TestSystem sys("bin", "myhome");
    sys.envSet("ANDROID_SDK_HOME", "android-sdk");
    EXPECT_THAT(ConfigDirs::getUserDirectory(), PathEq("android-sdk"));

    sys.getTempRoot()->makeSubDir(fs::path("android-sdk"));
    sys.getTempRoot()->makeSubDir(fs::path("android-sdk") / ".android");
    EXPECT_THAT(ConfigDirs::getUserDirectory(), PathEq("android-sdk/.android"));
}

TEST(ConfigDirs, getUserDirectoryWithAndroidSdkHomeAndPrefsRoot) {
    TestSystem sys("bin", "myhome");
    sys.envSet("ANDROID_SDK_HOME", "android-sdk");
    sys.envSet("ANDROID_SDK_HOME", "android-sdk-new");
    EXPECT_THAT(ConfigDirs::getUserDirectory(), PathEq("android-sdk-new"));

    sys.getTempRoot()->makeSubDir(fs::path("android-sdk-new"));
    sys.getTempRoot()->makeSubDir(fs::path("android-sdk-new") / ".android");
    EXPECT_THAT(ConfigDirs::getUserDirectory(), PathEq("android-sdk-new/.android"));
}

TEST(ConfigDirs, getUserDirectoryWithAndroidEmulatorHome) {
    TestSystem sys("bin", "myhome");
    sys.envSet("ANDROID_EMULATOR_HOME",
               "android"
               "home");
    EXPECT_THAT(ConfigDirs::getUserDirectory(), PathEq("androidhome"));
}

TEST(ConfigDirs, getSdkRootDirectory) {
    TestSystem sys("", "myhome");
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Sdk")));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Sdk") / "platform-tools"));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Sdk") / "platforms"));
    ASSERT_TRUE(sys.pathIsDir("Sdk"));

    sys.envSet("ANDROID_SDK_ROOT", "Sdk");
    EXPECT_THAT(ConfigDirs::getSdkRootDirectory(true), PathEq("Sdk"));

    sys.envSet("ANDROID_SDK_ROOT",
               "\""
               "Sdk\"");
    EXPECT_THAT(ConfigDirs::getSdkRootDirectory(true), PathEq("Sdk"));

    sys.envSet("ANDROID_SDK_ROOT", "");
    EXPECT_THAT(ConfigDirs::getSdkRootDirectory(true), Not(PathEq("Sdk")));

    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Sdk2")));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Sdk2") / "platform-tools"));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("Sdk2") / "platforms"));
    ASSERT_TRUE(sys.pathIsDir("Sdk2"));

    // ANDROID_HOME should take precedence over ANDROID_SDK_ROOT
    sys.envSet("ANDROID_HOME", "Sdk2");
    EXPECT_THAT(ConfigDirs::getSdkRootDirectory(true), PathEq("Sdk2"));

    // Bad ANDROID_HOME falls back to ANDROID_SDK_ROOT
    sys.envSet("ANDROID_HOME", "bogus");
    sys.envSet("ANDROID_SDK_ROOT", "Sdk");
    EXPECT_THAT(ConfigDirs::getSdkRootDirectory(true), PathEq("Sdk"));
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
    ASSERT_TRUE(sys.pathIsDir("Area_1/.android/avd"));
    ASSERT_TRUE(sys.pathIsDir("Area_2/.android/avd"));
    ASSERT_TRUE(sys.pathIsDir("Area_3/.android/avd"));
    ASSERT_TRUE(sys.pathIsDir("Area_4/.android/avd"));
    ASSERT_TRUE(sys.pathIsDir("Area_5/.android/avd"));
    ASSERT_TRUE(sys.pathIsDir("Area_6/.android/avd"));

    // Order of precedence is
    //   ANDROID_AVD_HOME
    //   ANDROID_PREFS_ROOT
    //   ANDROID_SDK_HOME
    //   TEMP_TSTDIR
    //   USER_HOME or HOME
    //   ANDROID_EMULATOR_HOME

    sys.envSet("ANDROID_AVD_HOME", "Area_1/.android/avd");
    sys.envSet("ANDROID_PREFS_ROOT", "Area_2");
    sys.envSet("ANDROID_SDK_HOME", "Area_3");
    sys.envSet("TEST_TMPDIR", "Area_4");
    sys.envSet("USER_HOME", "Area_5");
    sys.envSet("ANDROID_EMULATOR_HOME", "Area_6/.android");
    EXPECT_THAT(ConfigDirs::getAvdRootDirectory(), PathEq("Area_1/.android/avd"));

    sys.envSet("ANDROID_AVD_HOME", "bogus");
    EXPECT_THAT(ConfigDirs::getAvdRootDirectory(), PathEq("Area_2/.android/avd"));

    sys.envSet("ANDROID_PREFS_ROOT", "bogus");
    EXPECT_THAT(ConfigDirs::getAvdRootDirectory(), PathEq("Area_3/.android/avd"));

    sys.envSet("ANDROID_SDK_HOME", "bogus");
    EXPECT_THAT(ConfigDirs::getAvdRootDirectory(), PathEq("Area_4/.android/avd"));

    sys.envSet("TEST_TMPDIR", "bogus");
    EXPECT_THAT(ConfigDirs::getAvdRootDirectory(), PathEq("Area_5/.android/avd"));

    sys.envSet("USER_HOME", "bogus");
    EXPECT_THAT(ConfigDirs::getAvdRootDirectory(), PathEq("Area_6/.android/avd"));
}
