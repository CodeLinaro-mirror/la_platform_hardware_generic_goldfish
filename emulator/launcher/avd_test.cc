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
#include "android/goldfish/avd.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iostream>

#include "absl/log/globals.h"
#include "absl/status/status_matchers.h"

#include "android/base/testing/TestSystem.h"
#include "android/base/testing/TestTempDir.h"
#include "android/goldfish/input_paths.h"
#include "android/status/status_matcher_macros.h"

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using android::base::TestSystem;
using android::base::TestTempDir;

namespace android::goldfish::avd {

void writeToFile(fs::path path, std::string text) {
    std::ofstream iniFile(path, std::ios::trunc);
    iniFile << text;
    iniFile.close();
}

ResolvedInputPaths setupPaths(TestTempDir* tmp) {
    fs::path android_home("android_home");
    tmp->makeSubDir(android_home);
    tmp->makeSubDir(android_home / "avd");

    return {
        .user_directory = tmp->path(),
        .avd_directory = tmp->path() / android_home / "avd",
        .sdk_directory = tmp->path() / android_home,
    };
}

fs::path createTestAvd(const ResolvedInputPaths& paths, const std::string& targetString) {
    fs::path avd_dir = paths.avd_directory / "test_avd.avd";
    base::file::mkdir_recursive(avd_dir, 0755).IgnoreError();

    // Create an ini file for the test AVD
    writeToFile(paths.avd_directory / "test_avd.ini", absl::StrCat("path=", avd_dir.string()));

    // Set the 'target' property in the config.ini file
    writeToFile(avd_dir / "config.ini", "target=" + targetString);

    return avd_dir;
}

TEST(Avd, api_level) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    auto paths = setupPaths(tmp);

    createTestAvd(paths, "android-30");

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(paths, "test_avd"));
    EXPECT_EQ(avd->ApiLevel(), 30);
}

TEST(Avd, dessert) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    auto paths = setupPaths(tmp);

    createTestAvd(paths, "android-30");

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(paths, "test_avd"));
    EXPECT_EQ(avd->Dessert(), "R");
}

TEST(Avd, unknownApiLevel) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    auto paths = setupPaths(tmp);

    createTestAvd(paths, "android-1");  // API level 1 doesn't have a dessert name

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(paths, "test_avd"));
    EXPECT_EQ(avd->ApiLevel(), 3);  // Should default to API level 3
    EXPECT_EQ(avd->Dessert(), "");  // No dessert name for API level 1
}

TEST(Avd, invalidTargetFormat) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    auto paths = setupPaths(tmp);

    createTestAvd(paths, "invalid-target-format");

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(paths, "test_avd"));
    EXPECT_EQ(avd->ApiLevel(), Avd::kUnknownApiLevel);  // Should return the unknown API level
    EXPECT_EQ(avd->Dessert(), "");                      // No dessert name for invalid API level
}

TEST(Avd, path_getAvdSystemPath) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    auto paths = setupPaths(tmp);
    fs::path android_home("android_home");
    tmp->makeSubDir(android_home / "sysimg");
    tmp->makeSubDir("nothome");

    fs::path avd_dir = paths.avd_directory / "q.avd";
    base::file::mkdir_recursive(avd_dir, 0755).IgnoreError();
    writeToFile(paths.avd_directory / "q.ini", absl::StrCat("path=", avd_dir.string()));
    writeToFile(avd_dir / "config.ini", "image.sysdir.1=sysimg");

    auto inis = Avd::List(paths.avd_directory);
    EXPECT_EQ(1, inis.size());
}

TEST(Avd, path_getAvdSystemImage) {
    absl::SetGlobalVLogLevel(4);
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    auto paths = setupPaths(tmp);
    fs::path android_home("android_home");
    tmp->makeSubDir(android_home / "sysimg");
    tmp->makeSubDir("nothome");
    tmp->makeSubDir(fs::path("nothome") / "blah");

    // Create an in file for the @q avd.
    fs::path avd_dir = paths.avd_directory / "q.avd";
    base::file::mkdir_recursive(avd_dir, 0755).IgnoreError();
    writeToFile(paths.avd_directory / "q.ini", absl::StrCat("path=", avd_dir.string()));
    writeToFile(avd_dir / "config.ini", "image.sysdir.1=sysimg");

    auto inis = Avd::List(paths.avd_directory);
    EXPECT_EQ(1, inis.size());

    // No override.
    auto expectedPath = tmp->path() / android_home / "sysimg" / "system.img";
    writeToFile(expectedPath, "some data");

    ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(paths, "q"));
    EXPECT_THAT(avd->GetSystemImageFilePath(Avd::ImageType::INITSYSTEM),
                IsOkAndHolds(expectedPath));

    std::remove(expectedPath.string().c_str());

    // Override.
    expectedPath = tmp->path() / "nothome" / "blah" / "system.img";
    writeToFile(expectedPath, "some data");

    ASSERT_OK_AND_ASSIGN(auto avd2, Avd::FromName(paths, "q", /*wipe_data=*/false, tmp->path() / "nothome" / "blah"));
    EXPECT_THAT(avd2->GetSystemImageFilePath(Avd::ImageType::INITSYSTEM),
                IsOkAndHolds(expectedPath));
}

TEST(Avd, wipe_data) {
    TestSystem sys("/home", "/");
    TestTempDir* tmp = sys.getTempRoot();
    auto paths = setupPaths(tmp);
    auto avd_dir = createTestAvd(paths, "android-30");

    auto some_file = avd_dir / "some-file";
    auto some_subdir = avd_dir / "some-subdir";
    auto some_subdir_file = avd_dir / "some-subdir" / "some-subdir-file";
    base::file::touch(some_file).IgnoreError();
    base::file::mkdir_recursive(some_subdir, 0755).IgnoreError();
    base::file::touch(some_subdir_file).IgnoreError();

    {
        ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(paths, "test_avd", /*wipe_data=*/false));
        EXPECT_TRUE(base::file::exists(some_file));
        EXPECT_TRUE(base::file::exists(some_subdir));
        EXPECT_TRUE(base::file::exists(some_subdir_file));

        EXPECT_TRUE(base::file::exists(avd_dir/"config.ini"));
    }

    {
        ASSERT_OK_AND_ASSIGN(auto avd, Avd::FromName(paths, "test_avd", /*wipe_data=*/true));
        EXPECT_FALSE(base::file::exists(some_file));
        EXPECT_FALSE(base::file::exists(some_subdir));
        EXPECT_FALSE(base::file::exists(some_subdir_file));

        EXPECT_TRUE(base::file::exists(avd_dir/"config.ini"));
    }
}

}  // namespace android::goldfish::avd
