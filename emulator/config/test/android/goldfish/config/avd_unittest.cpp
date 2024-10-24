// Copyright 2014 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include "android/goldfish/config/avd.h"

#include <gtest/gtest.h>

#include <fstream>
#include <iostream>
#include <memory>

#include "aemu/base/ArraySize.h"
#include "aemu/base/files/PathUtils.h"
#include "aemu/base/memory/ScopedPtr.h"
#include "android/base/testing/TestSystem.h"
#include "android/base/testing/TestTempDir.h"
#include "android/goldfish/config/config_dirs.h"

using android::base::ScopedCPtr;
using android::base::TestSystem;
using android::base::TestTempDir;

namespace android::goldfish::avd {
static fs::path pj(fs::path a, fs::path b) {
    return a / b;
}

void writeToFile(fs::path path, std::string text) {
    std::ofstream iniFile(path, std::ios::trunc);
    iniFile << text;
    iniFile.close();
}

void createTestAvd(TestSystem& sys, TestTempDir* tmp, const std::string& targetString) {
    std::string sdkRoot = pj(tmp->pathString(), "android_home");
    std::string avdConfig = pj(pj(sdkRoot, "avd"), "config.ini");
    sys.envSet("ANDROID_AVD_HOME", sdkRoot);

    // Create an ini file for the test AVD
    writeToFile(pj(sdkRoot, "test_avd.ini"), std::string("path=") + pj(sdkRoot, "avd").string());

    // Set the 'target' property in the config.ini file
    writeToFile(avdConfig, "target=" + targetString);
}

TEST(Avd, apiLevel) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    tmp->makeSubDir("android_home");
    tmp->makeSubDir(pj("android_home", "avd"));

    createTestAvd(sys, tmp, "android-30");

    auto avdResult = Avd::fromName("test_avd");
    ASSERT_TRUE(avdResult.ok());
    Avd avd = std::move(avdResult.value());

    EXPECT_EQ(avd.apiLevel(), 30);
}

TEST(Avd, dessert) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    tmp->makeSubDir("android_home");
    tmp->makeSubDir(pj("android_home", "avd"));

    createTestAvd(sys, tmp, "android-30");

    auto avdResult = Avd::fromName("test_avd");
    ASSERT_TRUE(avdResult.ok());
    Avd avd = std::move(avdResult.value());

    EXPECT_EQ(avd.dessert(), "R");
}

TEST(Avd, unknownApiLevel) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    tmp->makeSubDir("android_home");
    tmp->makeSubDir(pj("android_home", "avd"));

    createTestAvd(sys, tmp, "android-1");  // API level 1 doesn't have a dessert name

    auto avdResult = Avd::fromName("test_avd");
    ASSERT_TRUE(avdResult.ok());
    Avd avd = std::move(avdResult.value());

    EXPECT_EQ(avd.apiLevel(), 3);  // Should default to API level 3
    EXPECT_EQ(avd.dessert(), "");  // No dessert name for API level 1
}

TEST(Avd, invalidTargetFormat) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    tmp->makeSubDir("android_home");
    tmp->makeSubDir(pj("android_home", "avd"));

    createTestAvd(sys, tmp, "invalid-target-format");

    auto avdResult = Avd::fromName("test_avd");
    ASSERT_TRUE(avdResult.ok());
    Avd avd = std::move(avdResult.value());

    EXPECT_EQ(avd.apiLevel(), Avd::kUnknownApiLevel);  // Should return the unknown API level
    EXPECT_EQ(avd.dessert(), "");                      // No dessert name for invalid API level
}

TEST(Avd, path_getAvdSystemPath) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    tmp->makeSubDir("android_home");
    tmp->makeSubDir(pj("android_home", "sysimg"));
    tmp->makeSubDir(pj("android_home", "avd"));
    tmp->makeSubDir("nothome");

    std::string sdkRoot = pj(tmp->pathString(), "android_home");
    std::string avdConfig = pj(pj(sdkRoot, "avd"), "config.ini");
    sys.envSet("ANDROID_AVD_HOME", sdkRoot);
    EXPECT_EQ(ConfigDirs::getAvdRootDirectory().string(), tmp->path() / "android_home");

    // Create an in file for the @q avd.
    writeToFile(pj(sdkRoot, "q.ini"), std::string("path=") + pj(sdkRoot, "avd").string());

    // A relative path should be resolved from ANRDOID_AVD_HOME
    writeToFile(avdConfig, "image.sysdir.1=sysimg");

    auto inis = Avd::list();
    EXPECT_EQ(1, inis.size());
}
}  // namespace android::goldfish::avd