// Copyright (C) 2026 The Android Open Source Project
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

#include "goldfish/discovery/emulator_advertisement.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "absl/status/status_matchers.h"
#include "absl/strings/str_format.h"

#include "android/base/testing/TestSystem.h"
#include "android/process/process.h"
#include "goldfish/file/file.h"

namespace goldfish::discovery {

namespace fs = std::filesystem;
using android::base::TestSystem;
using ::testing::Not;

class TestEmulatorAdvertisement : public EmulatorAdvertisement {
  public:
    TestEmulatorAdvertisement(std::filesystem::path discovery_directory,
                              LivenessChecker liveness_checker = EmulatorAdvertisement::IsPidAlive)
            : EmulatorAdvertisement(discovery_directory, liveness_checker) {}

    int GarbageCollect() const { return EmulatorAdvertisement::GarbageCollect(); }

    static bool TestIsPidAlive(fs::path my_file, fs::path discovery_file) {
        return EmulatorAdvertisement::IsPidAlive(my_file, discovery_file);
    }

    absl::Status Remove() const { return EmulatorAdvertisement::Remove(); }
};

class EmulatorAdvertisementTest : public ::testing::Test {
  protected:
    void SetUp() override {
        const char* tmpdir = std::getenv("TEST_TMPDIR");
        if (tmpdir) {
            test_dir_ = fs::path(tmpdir) / "discovery_test";
        } else {
            test_dir_ = fs::temp_directory_path() / "discovery_test";
        }
        fs::create_directories(test_dir_);
    }

    void TearDown() override { fs::remove_all(test_dir_); }

    fs::path test_dir_;
};

TEST_F(EmulatorAdvertisementTest, WriteAndRemove) {
    EmulatorProperties props = {{"key1", "val1"}, {"key2", "val2"}};

    // Mock liveness checker that considers everything alive.
    auto checker = [](fs::path my_file, fs::path discovery_file) { return true; };

    {
        EmulatorAdvertisement ad(test_dir_, checker);

        EXPECT_THAT(ad.Write(props), absl_testing::IsOk());

        // Verify file exists and contains properties.
        bool found = false;
        for (const auto& entry : fs::directory_iterator(test_dir_)) {
            if (entry.path().extension() == ".ini") {
                found = true;
                std::ifstream ifs(entry.path());
                std::string line;
                bool has_key1 = false;
                bool has_key2 = false;
                while (std::getline(ifs, line)) {
                    if (line == "key1=val1") has_key1 = true;
                    if (line == "key2=val2") has_key2 = true;
                }
                EXPECT_TRUE(has_key1);
                EXPECT_TRUE(has_key2);
            }
        }
        EXPECT_TRUE(found);
    }

    // Verify file is removed.
    EXPECT_TRUE(fs::is_empty(test_dir_));
}

TEST_F(EmulatorAdvertisementTest, GarbageCollect) {
    EmulatorProperties props = {{"key1", "val1"}};

    // Mock liveness checker that only considers our own file alive.
    auto checker = [](fs::path my_file, fs::path discovery_file) {
        return discovery_file == my_file;
    };

    TestEmulatorAdvertisement ad(test_dir_, checker);

    EXPECT_THAT(ad.Write(props), absl_testing::IsOk());

    // Create a fake stale file.
    fs::path dead_file = test_dir_ / "pid_999.ini";
    {
        std::ofstream ofs(dead_file);
        ofs << "key=val\n";
    }

    EXPECT_TRUE(fs::exists(dead_file));

    int collected = ad.GarbageCollect();
    EXPECT_EQ(collected, 1);

    // Verify stale file is removed.
    EXPECT_FALSE(fs::exists(dead_file));
}

TEST_F(EmulatorAdvertisementTest, DiscoverRunningEmulators) {
    EmulatorProperties props = {{"key1", "val1"}};

    // Mock liveness checker that considers everything alive.
    auto checker = [](fs::path my_file, fs::path discovery_file) { return true; };

    EmulatorAdvertisement ad(test_dir_, checker);
    EXPECT_THAT(ad.Write(props), absl_testing::IsOk());

    // Create another fake active file.
    fs::path other_file = test_dir_ / "pid_888.ini";
    {
        std::ofstream ofs(other_file);
        ofs << "key=val\n";
    }

    auto discovered = ad.DiscoverRunningEmulators();
    EXPECT_THAT(discovered, absl_testing::IsOk());

    // Should discover the other file, but not ours.
    EXPECT_EQ(discovered->size(), 1);
    EXPECT_EQ((*discovered)[0].filename(), "pid_888.ini");
}

TEST_F(EmulatorAdvertisementTest, IsPidAliveCurrentProcess) {
    fs::path my_file = test_dir_ / "pid_1.ini";
    fs::path active_file =
            test_dir_ / absl::StrFormat("pid_%d.ini", android::base::Process::Me()->pid());
    {
        std::ofstream ofs(active_file);
        ofs << "key=val\n";
    }
    EXPECT_TRUE(TestEmulatorAdvertisement::TestIsPidAlive(my_file, active_file));
}

TEST_F(EmulatorAdvertisementTest, IsPidAliveDeadProcess) {
    fs::path my_file = test_dir_ / "pid_1.ini";
    fs::path dead_file = test_dir_ / "pid_999999.ini";
    EXPECT_FALSE(TestEmulatorAdvertisement::TestIsPidAlive(my_file, dead_file));
}

TEST_F(EmulatorAdvertisementTest, IsPidAliveMalformedFilename) {
    fs::path my_file = test_dir_ / "pid_1.ini";
    fs::path bad_file = test_dir_ / "not_a_pid.ini";
    EXPECT_FALSE(TestEmulatorAdvertisement::TestIsPidAlive(my_file, bad_file));
}

TEST_F(EmulatorAdvertisementTest, IsPidAliveOtherProcess) {
    fs::path my_file = test_dir_ / "pid_1.ini";
    fs::path other_file = test_dir_ / "pid_1.ini";
    EXPECT_FALSE(TestEmulatorAdvertisement::TestIsPidAlive(my_file, other_file));
}

TEST_F(EmulatorAdvertisementTest, DiscoverRunningEmulatorsMissingDirectory) {
    EmulatorAdvertisement ad(test_dir_);
    fs::remove_all(test_dir_);
    auto discovered = ad.DiscoverRunningEmulators();
    EXPECT_THAT(discovered, Not(absl_testing::IsOk()));
}

TEST_F(EmulatorAdvertisementTest, CreateJwkDirectory) {
    EmulatorAdvertisement ad(test_dir_);
    std::string token = "test_token";
    auto res = ad.CreateJwkDirectory(token);
    EXPECT_THAT(res, absl_testing::IsOk());

    fs::path expected_path =
            test_dir_ / std::to_string(android::base::Process::Me()->pid()) / "jwks" / token;
    EXPECT_EQ(*res, expected_path);
    EXPECT_TRUE(fs::is_directory(expected_path));

    // Verify permissions are 0700
    EXPECT_THAT(android::base::file::mode(expected_path),
                absl_testing::IsOkAndHolds(
                        ::testing::Truly([](unsigned mode) { return (mode & 0700) == 0700; })));
}

TEST_F(EmulatorAdvertisementTest, RemoveIdempotent) {
    EmulatorProperties props = {{"key1", "val1"}};
    TestEmulatorAdvertisement ad(test_dir_);
    EXPECT_THAT(ad.Write(props), absl_testing::IsOk());
    EXPECT_THAT(ad.Remove(), absl_testing::IsOk());
    EXPECT_THAT(ad.Remove(), absl_testing::IsOk());
}

TEST_F(EmulatorAdvertisementTest, WritePermissionDenied) {
#ifdef _WIN32
    GTEST_SKIP() << "Skipping test on Windows due to different chmod behavior.";
#endif
    EmulatorProperties props = {{"key1", "val1"}};
    EmulatorAdvertisement ad(test_dir_);
    android::base::file::chmod(test_dir_, 0444).IgnoreError();
    auto status = ad.Write(props);
    EXPECT_THAT(status, Not(absl_testing::IsOk()));
    android::base::file::chmod(test_dir_, 0777).IgnoreError();
}

class ConfigDirsTest : public ::testing::TestWithParam<bool> {};

TEST_P(ConfigDirsTest, getDiscoveryDirectory) {
    TestSystem sys("", "myhome");

    ASSERT_TRUE(sys.GetTempRoot()->MakeSubDir(fs::path("runtime")));
    fs::path base = sys.GetTempRoot()->Path() / "runtime";

#if defined(_WIN32)
    base = base / "Temp";
#elif defined(__APPLE__)
    base = base / "Library" / "Caches" / "TemporaryItems";
#endif

    auto want = base / "avd" / "running";
    if (GetParam()) {
        ASSERT_TRUE(
                android::base::file::mkdir_recursive(sys.GetTempRoot()->Path() / want, 0755).ok())
                << "creating: " << want;
        // Make sure that unreadable dir can be fixed.
        android::base::file::chmod(want, 0055).IgnoreError();
    }

    sys.EnvSet("LOCALAPPDATA", (sys.GetTempRoot()->Path() / "runtime").string());
    sys.EnvSet("XDG_RUNTIME_DIR", (sys.GetTempRoot()->Path() / "runtime").string());
    sys.EnvSet("HOME", (sys.GetTempRoot()->Path() / "runtime").string());

    auto got = EmulatorAdvertisement::GetDiscoveryDirectory();
    EXPECT_THAT(got.string(), testing::EndsWith(want.string()));
    EXPECT_TRUE(android::base::file::exists(got));
    // On Windows this will return 0777 (instead of 0755) as there's only one read-only bit.
    EXPECT_THAT(android::base::file::mode(got),
                absl_testing::IsOkAndHolds(
                        ::testing::Truly([](unsigned mode) { return (mode & 0700) == 0700; })));
}

INSTANTIATE_TEST_SUITE_P(DiscoveryDirectory, ConfigDirsTest, testing::Values(true, false));

}  // namespace goldfish::discovery
