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

#include <filesystem>
#include <fstream>
#include <iostream>

#include "absl/log/globals.h"
#include "absl/status/status_matchers.h"

#include "aemu/base/memory/ScopedPtr.h"
#include "aemu/base/utils/status_matcher_macros.h"
#include "android/base/testing/TestSystem.h"
#include "android/base/testing/TestTempDir.h"
#include "android/goldfish/config/fake-avd.h"
#include "android/goldfish/input_paths.h"

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

ResolvedInputPaths setupPaths(TestTempDir *tmp) {
    tmp->makeSubDir("android_home");
    tmp->makeSubDir(pj("android_home", "avd"));

    return {
        .user_directory = tmp->path(),
        .avd_directory = tmp->path() / "android_home" / "avd",
        .sdk_directory = tmp->path() / "android_home",
    };
}

void createTestAvd(const ResolvedInputPaths &paths, const std::string& targetString) {
    fs::path avd_dir = paths.avd_directory / "test_avd.avd";
    fs::create_directories(avd_dir);

    // Create an ini file for the test AVD
    writeToFile(paths.avd_directory / "test_avd.ini", absl::StrCat("path=", avd_dir.string()));

    // Set the 'target' property in the config.ini file
    writeToFile(avd_dir / "config.ini", "target=" + targetString);
}

TEST(Avd, apiLevel) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    auto paths = setupPaths(tmp);

    createTestAvd(paths, "android-30");

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::fromName(paths, "test_avd"));
    EXPECT_EQ(avd->apiLevel(), 30);
}

TEST(Avd, dessert) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    auto paths = setupPaths(tmp);

    createTestAvd(paths, "android-30");

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::fromName(paths, "test_avd"));
    EXPECT_EQ(avd->dessert(), "R");
}

TEST(Avd, unknownApiLevel) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    auto paths = setupPaths(tmp);

    createTestAvd(paths, "android-1");  // API level 1 doesn't have a dessert name

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::fromName(paths, "test_avd"));
    EXPECT_EQ(avd->apiLevel(), 3);  // Should default to API level 3
    EXPECT_EQ(avd->dessert(), "");  // No dessert name for API level 1
}

TEST(Avd, invalidTargetFormat) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    auto paths = setupPaths(tmp);

    createTestAvd(paths, "invalid-target-format");

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::fromName(paths, "test_avd"));
    EXPECT_EQ(avd->apiLevel(), Avd::kUnknownApiLevel);  // Should return the unknown API level
    EXPECT_EQ(avd->dessert(), "");                      // No dessert name for invalid API level
}

TEST(Avd, path_getAvdSystemPath) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    auto paths = setupPaths(tmp);
    tmp->makeSubDir(pj("android_home", "sysimg"));
    tmp->makeSubDir("nothome");

    fs::path avd_dir = paths.avd_directory / "q.avd";
    fs::create_directories(avd_dir);
    writeToFile(paths.avd_directory / "q.ini", absl::StrCat("path=", avd_dir.string()));
    writeToFile(avd_dir / "config.ini", "image.sysdir.1=sysimg");

    auto inis = Avd::list(paths.avd_directory);
    EXPECT_EQ(1, inis.size());
}

TEST(Avd, path_getAvdSystemImage) {
    absl::SetGlobalVLogLevel(4);
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    auto paths = setupPaths(tmp);
    tmp->makeSubDir(pj("android_home", "sysimg"));
    tmp->makeSubDir("nothome");
    tmp->makeSubDir(pj("nothome", "blah"));

    // Create an in file for the @q avd.
    fs::path avd_dir = paths.avd_directory / "q.avd";
    fs::create_directories(avd_dir);
    writeToFile(paths.avd_directory / "q.ini", absl::StrCat("path=", avd_dir.string()));
    writeToFile(avd_dir / "config.ini", "image.sysdir.1=sysimg");

    auto inis = Avd::list(paths.avd_directory);
    EXPECT_EQ(1, inis.size());

    // No override.
    auto expectedPath = tmp->path() / "android_home" / "sysimg" / "system.img";
    writeToFile(expectedPath, "some data");

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::fromName(paths, "q"));
    EXPECT_THAT(avd->getSystemImageFilePath(Avd::ImageType::INITSYSTEM), IsOkAndHolds(expectedPath));

    std::remove(expectedPath.string().c_str());

    // Override.
    expectedPath = tmp->path() / "nothome" / "blah" / "system.img";
    writeToFile(expectedPath, "some data");

    ASSERT_OK_AND_ASSIGN(auto avd2, Avd::fromName(paths, "q", tmp->path() / "nothome" / "blah"));
    EXPECT_THAT(avd2->getSystemImageFilePath(Avd::ImageType::INITSYSTEM), IsOkAndHolds(expectedPath));
}

TEST(FakeAvdTest, DefaultValues) {
    FakeAvd avd;

    EXPECT_EQ(avd.name(), "default_fake_avd");
    EXPECT_EQ(avd.getContentPath(), "/tmp/fake_avd");
    EXPECT_EQ(avd.apiLevel(), 35);
    EXPECT_EQ(avd.dessert(), "V");
    EXPECT_EQ(avd.apiDescription(), "15.0 (V) - API 35");
    EXPECT_TRUE(avd.playstore());
    EXPECT_EQ(avd.getDeviceType(), DeviceType::kPhone);
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

    avd.setDeviceType(DeviceType::kTv);
    EXPECT_EQ(avd.getDeviceType(), DeviceType::kTv);

    avd.setCpuArchitecture(Avd::CpuArchitecture::kX86);
    EXPECT_EQ(avd.detectArchitecture(), Avd::CpuArchitecture::kX86);

    avd.setDisplayName("My Display Name");
    EXPECT_EQ(avd.display_name(), "My Display Name");
}

}  // namespace android::goldfish::avd
