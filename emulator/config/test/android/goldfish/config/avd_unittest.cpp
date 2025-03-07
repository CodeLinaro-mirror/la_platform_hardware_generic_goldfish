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

#include "absl/log/globals.h"
#include "absl/status/status_matchers.h"

#include "aemu/base/ArraySize.h"
#include "aemu/base/files/PathUtils.h"
#include "aemu/base/memory/ScopedPtr.h"
#include "aemu/base/utils/status_matcher_macros.h"
#include "android/base/testing/TestSystem.h"
#include "android/base/testing/TestTempDir.h"
#include "android/goldfish/config/config_dirs.h"
#include "android/goldfish/config/fake-avd.h"

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
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

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::fromName("test_avd"));
    EXPECT_EQ(avd->apiLevel(), 30);
}

TEST(Avd, dessert) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    tmp->makeSubDir("android_home");
    tmp->makeSubDir(pj("android_home", "avd"));

    createTestAvd(sys, tmp, "android-30");

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::fromName("test_avd"));
    EXPECT_EQ(avd->dessert(), "R");
}

TEST(Avd, unknownApiLevel) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    tmp->makeSubDir("android_home");
    tmp->makeSubDir(pj("android_home", "avd"));

    createTestAvd(sys, tmp, "android-1");  // API level 1 doesn't have a dessert name

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::fromName("test_avd"));
    EXPECT_EQ(avd->apiLevel(), 3);  // Should default to API level 3
    EXPECT_EQ(avd->dessert(), "");  // No dessert name for API level 1
}

TEST(Avd, invalidTargetFormat) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    tmp->makeSubDir("android_home");
    tmp->makeSubDir(pj("android_home", "avd"));

    createTestAvd(sys, tmp, "invalid-target-format");

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::fromName("test_avd"));
    EXPECT_EQ(avd->apiLevel(), Avd::kUnknownApiLevel);  // Should return the unknown API level
    EXPECT_EQ(avd->dessert(), "");                      // No dessert name for invalid API level
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

TEST(Avd, path_getAvdSystemImage) {
    absl::SetGlobalVLogLevel(4);
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    tmp->makeSubDir("android_home");
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("android_home") / "platform-tools"));
    ASSERT_TRUE(sys.getTempRoot()->makeSubDir(fs::path("android_home") / "platforms"));
    tmp->makeSubDir(pj("android_home", "sysimg"));
    tmp->makeSubDir(pj("android_home", "avd"));
    tmp->makeSubDir("nothome");
    tmp->makeSubDir(pj("nothome", "blah"));

    std::string sdkRoot = pj(tmp->pathString(), "android_home");
    sys.envSet("ANDROID_SDK_ROOT", sdkRoot);
    ASSERT_EQ(ConfigDirs::getSdkRootDirectory(true).string(), tmp->path() / "android_home");

    sys.envSet("ANDROID_AVD_HOME", sdkRoot);
    EXPECT_EQ(ConfigDirs::getAvdRootDirectory().string(), tmp->path() / "android_home");

    // Create an in file for the @q avd.
    writeToFile(pj(sdkRoot, "q.ini"), std::string("path=") + pj(sdkRoot, "avd").string());

    // A relative path should be resolved from ANDROID_AVD_HOME
    std::string avdConfig = pj(pj(sdkRoot, "avd"), "config.ini");
    writeToFile(avdConfig, "image.sysdir.1=sysimg");

    auto inis = Avd::list();
    EXPECT_EQ(1, inis.size());

    // No override.
    auto expectedPath = tmp->path() / "android_home" / "sysimg" / "system.img";
    writeToFile(expectedPath, "some data");

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::fromName("q"));
    EXPECT_THAT(avd->getImageFilePath(Avd::ImageType::INITSYSTEM), IsOkAndHolds(expectedPath));

    std::remove(expectedPath.string().c_str());

    // Override.
    expectedPath = tmp->path() / "nothome" / "blah" / "system.img";
    writeToFile(expectedPath, "some data");

    ASSERT_OK_AND_ASSIGN(auto avd2, Avd::fromName("q", tmp->path() / "nothome" / "blah"));
    EXPECT_THAT(avd2->getImageFilePath(Avd::ImageType::INITSYSTEM), IsOkAndHolds(expectedPath));
}

TEST(FakeAvdTest, DefaultValues) {
    FakeAvd avd;

    EXPECT_EQ(avd.name(), "default_fake_avd");
    EXPECT_EQ(avd.getContentPath(), "/tmp/fake_avd");
    EXPECT_EQ(avd.apiLevel(), 35);
    EXPECT_EQ(avd.dessert(), "V");
    EXPECT_EQ(avd.apiDescription(), "15.0 (V) - API 35");
    EXPECT_TRUE(avd.playstore());
    EXPECT_EQ(avd.getDeviceType(), Avd::DeviceType::kPhone);
    EXPECT_EQ(avd.detectArchitecture(), Avd::CpuArchitecture::kArm);

    // Check some HardwareConfig values
    EXPECT_EQ(avd.hw().hw_cpu_arch, "arm64");
    EXPECT_EQ(avd.hw().hw_cpu_ncore, 4);
    EXPECT_EQ(avd.hw().hw_ramSize, 2048);
    EXPECT_EQ(avd.hw().hw_lcd_width, 1080);
    EXPECT_EQ(avd.hw().hw_lcd_height, 2424);
    EXPECT_EQ(avd.hw().hw_lcd_density, 420);
    EXPECT_EQ(avd.hw().disk_dataPartition_size, 6_GiB);
    EXPECT_TRUE(avd.hw().PlayStore_enabled);
}

TEST(FakeAvdTest, SettersAndGetters) {
    FakeAvd avd;

    avd.setName("test_avd");
    EXPECT_EQ(avd.name(), "test_avd");

    avd.setContentPath("/new/path");
    EXPECT_EQ(avd.getContentPath(), "/new/path");

    avd.setApiLevel(30);
    EXPECT_EQ(avd.apiLevel(), 30);

    avd.setDessert("Q");
    EXPECT_EQ(avd.dessert(), "Q");

    avd.setApiDescription("10.0 (Q) - API 30");
    EXPECT_EQ(avd.apiDescription(), "10.0 (Q) - API 30");

    avd.setPlaystore(false);
    EXPECT_FALSE(avd.playstore());

    avd.setDeviceType(Avd::DeviceType::kTv);
    EXPECT_EQ(avd.getDeviceType(), Avd::DeviceType::kTv);

    avd.setCpuArchitecture(Avd::CpuArchitecture::kX86);
    EXPECT_EQ(avd.detectArchitecture(), Avd::CpuArchitecture::kX86);

    avd.setHasEncryptionKey(true);
    EXPECT_TRUE(avd.hasEncryptionKey());

    avd.setDisplayName("My Display Name");
    EXPECT_EQ(avd.display_name(), "My Display Name");
}

}  // namespace android::goldfish::avd
