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

#include "trampoline.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>

#include "android/base/bazel_info.h"
#include "android/base/testing/test_system.h"
#include "android/goldfish/mock_avd.h"

using android::base::TestSystem;
using android::goldfish::MockAvd;
using android::goldfish::ShouldTrampolineToQemu2;
using ::testing::Return;
using ::testing::ReturnRef;

namespace {

fs::path CreateTempFeatureFile(TestSystem& sys, bool has_required_features) {
    fs::path temp_file = sys.GetTempRoot()->Path() / "advancedFeatures.ini";
    std::ofstream out(temp_file);
    if (has_required_features) {
        out << "mac80211hwsimUserspaceManaged=on\n";
    } else {
        out << "# Empty features\n";
    }
    out.close();
    return temp_file;
}

}  // namespace

TEST(TrampolineTest, shouldTrampolineIfApiLevelIsLessThan37) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("AEMU_NO_TRAMPOLINE", "");

    fs::path feature_file = CreateTempFeatureFile(sys, /*has_required_features=*/true);
    android::goldfish::SystemImagePaths paths;
    paths.advanced_features = feature_file;

    MockAvd avd;
    EXPECT_CALL(avd, ApiLevel()).WillRepeatedly(Return(35));
    EXPECT_CALL(avd, GetSystemImagePaths()).WillRepeatedly(ReturnRef(paths));

    EXPECT_TRUE(ShouldTrampolineToQemu2(avd));
}

TEST(TrampolineTest, shouldNotTrampolineIfApiLevelIs37OrGreaterAndHasRequiredFeatures) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("AEMU_NO_TRAMPOLINE", "");

    fs::path feature_file = CreateTempFeatureFile(sys, /*has_required_features=*/true);
    android::goldfish::SystemImagePaths paths;
    paths.advanced_features = feature_file;

    MockAvd avd;
    EXPECT_CALL(avd, ApiLevel()).WillRepeatedly(Return(37));
    EXPECT_CALL(avd, GetSystemImagePaths()).WillRepeatedly(ReturnRef(paths));

    EXPECT_FALSE(ShouldTrampolineToQemu2(avd));

    MockAvd avd_high;
    EXPECT_CALL(avd_high, ApiLevel()).WillRepeatedly(Return(38));
    EXPECT_CALL(avd_high, GetSystemImagePaths()).WillRepeatedly(ReturnRef(paths));

    EXPECT_FALSE(ShouldTrampolineToQemu2(avd_high));
}

TEST(TrampolineTest, shouldTrampolineIfRequiredFeatureIsMissing) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("AEMU_NO_TRAMPOLINE", "");

    fs::path feature_file = CreateTempFeatureFile(sys, /*has_required_features=*/false);
    android::goldfish::SystemImagePaths paths;
    paths.advanced_features = feature_file;

    MockAvd avd;
    EXPECT_CALL(avd, ApiLevel()).WillRepeatedly(Return(37));
    EXPECT_CALL(avd, GetSystemImagePaths()).WillRepeatedly(ReturnRef(paths));

    EXPECT_TRUE(ShouldTrampolineToQemu2(avd));

    MockAvd avd_high;
    EXPECT_CALL(avd_high, ApiLevel()).WillRepeatedly(Return(38));
    EXPECT_CALL(avd_high, GetSystemImagePaths()).WillRepeatedly(ReturnRef(paths));

    EXPECT_TRUE(ShouldTrampolineToQemu2(avd_high));
}

TEST(TrampolineTest, shouldNotTrampolineIfEnvVarIsSet) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("AEMU_NO_TRAMPOLINE", "1");

    MockAvd avd;
    EXPECT_CALL(avd, ApiLevel()).Times(0);

    EXPECT_FALSE(ShouldTrampolineToQemu2(avd));
}

TEST(TrampolineTest, shouldTrampolineIfForcedVersionIs2) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("AEMU_NO_TRAMPOLINE", "");

    MockAvd avd;
    EXPECT_CALL(avd, ForcedTrampolineVersion()).WillRepeatedly(Return(2));

    EXPECT_TRUE(ShouldTrampolineToQemu2(avd));
}

TEST(TrampolineTest, shouldNotTrampolineIfForcedVersionIs10OrGreater) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("AEMU_NO_TRAMPOLINE", "");

    MockAvd avd;
    EXPECT_CALL(avd, ForcedTrampolineVersion()).WillRepeatedly(Return(10));

    EXPECT_FALSE(ShouldTrampolineToQemu2(avd));
}

TEST(TrampolineTest, shouldTrampolineIfLastRunVersionIs2) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("AEMU_NO_TRAMPOLINE", "");

    fs::path feature_file = CreateTempFeatureFile(sys, /*has_required_features=*/true);
    android::goldfish::SystemImagePaths paths;
    paths.advanced_features = feature_file;

    MockAvd avd;
    EXPECT_CALL(avd, ForcedTrampolineVersion()).WillRepeatedly(Return(0));
    EXPECT_CALL(avd, GetSystemImagePaths()).WillRepeatedly(ReturnRef(paths));
    EXPECT_CALL(avd, GetDeviceType()).WillRepeatedly(Return(android::goldfish::DeviceType::kPhone));
    EXPECT_CALL(avd, ApiLevel()).WillRepeatedly(Return(37));
    EXPECT_CALL(avd, GetLastRunQemuVersion()).WillRepeatedly(Return(std::optional<int>(2)));

    EXPECT_TRUE(ShouldTrampolineToQemu2(avd));
}

TEST(TrampolineTest, shouldTrampolineIfDeviceIsNotPhone) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("AEMU_NO_TRAMPOLINE", "");

    fs::path feature_file = CreateTempFeatureFile(sys, /*has_required_features=*/true);
    android::goldfish::SystemImagePaths paths;
    paths.advanced_features = feature_file;

    MockAvd avd;
    EXPECT_CALL(avd, ForcedTrampolineVersion()).WillRepeatedly(Return(0));
    EXPECT_CALL(avd, GetLastRunQemuVersion()).WillRepeatedly(Return(std::optional<int>(1)));
    EXPECT_CALL(avd, GetSystemImagePaths()).WillRepeatedly(ReturnRef(paths));
    EXPECT_CALL(avd, GetDeviceType()).WillRepeatedly(Return(android::goldfish::DeviceType::kTv));

    EXPECT_TRUE(ShouldTrampolineToQemu2(avd));
}

TEST(TrampolineTest, shouldNotTrampolineIfDeviceIsPhoneAndApi37OrGreater) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("AEMU_NO_TRAMPOLINE", "");

    fs::path feature_file = CreateTempFeatureFile(sys, /*has_required_features=*/true);
    android::goldfish::SystemImagePaths paths;
    paths.advanced_features = feature_file;

    MockAvd avd;
    EXPECT_CALL(avd, ForcedTrampolineVersion()).WillRepeatedly(Return(0));
    EXPECT_CALL(avd, GetLastRunQemuVersion()).WillRepeatedly(Return(std::optional<int>(1)));
    EXPECT_CALL(avd, GetSystemImagePaths()).WillRepeatedly(ReturnRef(paths));
    EXPECT_CALL(avd, GetDeviceType()).WillRepeatedly(Return(android::goldfish::DeviceType::kPhone));
    EXPECT_CALL(avd, ApiLevel()).WillRepeatedly(Return(37));

    EXPECT_FALSE(ShouldTrampolineToQemu2(avd));
}

TEST(TrampolineTest, shouldNotTrampolineIfDeviceIsUnknownAndApi37OrGreater) {
    TestSystem sys("bin", "myhome");
    sys.EnvSet("AEMU_NO_TRAMPOLINE", "");

    fs::path feature_file = CreateTempFeatureFile(sys, /*has_required_features=*/true);
    android::goldfish::SystemImagePaths paths;
    paths.advanced_features = feature_file;

    MockAvd avd;
    EXPECT_CALL(avd, ForcedTrampolineVersion()).WillRepeatedly(Return(0));
    EXPECT_CALL(avd, GetLastRunQemuVersion()).WillRepeatedly(Return(std::optional<int>(1)));
    EXPECT_CALL(avd, GetSystemImagePaths()).WillRepeatedly(ReturnRef(paths));
    EXPECT_CALL(avd, GetDeviceType()).WillRepeatedly(Return(android::goldfish::DeviceType::kUnknown));
    EXPECT_CALL(avd, ApiLevel()).WillRepeatedly(Return(37));

    EXPECT_FALSE(ShouldTrampolineToQemu2(avd));
}

#ifdef _WIN32
#define EXE ".exe"
#define EMULATOR "emulator.exe"
#else
#define EXE ""
#define EMULATOR "emulator"
#endif

fs::path FindFakeLauncherMain() {
    return android::base::Bazel::RunfilesPath(
            absl::StrCat("goldfish+/emulator/launcher/fake_launcher_main", EXE));
}

TEST(TrampolineDeathTest, trampolineToQemu2Success) {
    fs::path mock_src = FindFakeLauncherMain();
    ASSERT_FALSE(mock_src.empty()) << "Could not find fake_launcher_main in runfiles!";

    TestSystem sys("bin", "myhome");
    fs::path temp_dir = sys.GetTempRoot()->Path();
    fs::path launcher_dir = temp_dir / "nested" / "bin";
    fs::path dest = temp_dir / "emulator" / EMULATOR;

    fs::create_directories(dest.parent_path());
    fs::copy_file(mock_src, dest, fs::copy_options::overwrite_existing);
    fs::permissions(dest, fs::perms::owner_all | fs::perms::group_exec | fs::perms::others_exec);

    EXPECT_EXIT(android::goldfish::TrampolineToQemu2(launcher_dir, {}),
                ::testing::ExitedWithCode(42), "");
}

TEST(TrampolineDeathTest, trampolineToQemu2ArgsForwarded) {
    fs::path mock_src = FindFakeLauncherMain();
    ASSERT_FALSE(mock_src.empty()) << "Could not find fake_launcher_main in runfiles!";

    TestSystem sys("bin", "myhome");
    fs::path temp_dir = sys.GetTempRoot()->Path();
    fs::path launcher_dir = temp_dir / "nested" / "bin";
    fs::path dest = temp_dir / "emulator" / EMULATOR;

    fs::create_directories(dest.parent_path());
    fs::copy_file(mock_src, dest, fs::copy_options::overwrite_existing);
    fs::permissions(dest, fs::perms::owner_all | fs::perms::group_exec | fs::perms::others_exec);

    EXPECT_EXIT(android::goldfish::TrampolineToQemu2(launcher_dir, {"foo", "bar"}),
                ::testing::ExitedWithCode(43), "");
}

TEST(TrampolineDeathTest, trampolineToQemu2BinaryNotExists) {
    TestSystem sys("bin", "myhome");
    fs::path temp_dir = sys.GetTempRoot()->Path();
    fs::path launcher_dir = temp_dir / "nested" / "bin";

    EXPECT_DEATH(
            android::goldfish::TrampolineToQemu2(launcher_dir, {}),
            ::testing::HasSubstr("Trying to trampoline but legacy emulator binary does not exist"));
}

TEST(TrampolineDeathTest, trampolineToQemu2BinaryNotExecutable) {
    fs::path mock_src = FindFakeLauncherMain();
    ASSERT_FALSE(mock_src.empty()) << "Could not find fake_launcher_main in runfiles!";

    TestSystem sys("bin", "myhome");
    fs::path temp_dir = sys.GetTempRoot()->Path();
    fs::path launcher_dir = temp_dir / "nested" / "bin";
    fs::path dest = temp_dir / "emulator" / EMULATOR;

    fs::create_directories(dest.parent_path());
    fs::copy_file(mock_src, dest, fs::copy_options::overwrite_existing);
    fs::permissions(dest, fs::perms::owner_read | fs::perms::owner_write);

    // TODO(whollins): Work out why this fails to match on Windows.
    EXPECT_DEATH(android::goldfish::TrampolineToQemu2(launcher_dir, {}),
#ifdef _WIN32
                 "");
#else
                 ::testing::HasSubstr(
                         "Trying to trampoline but cannot execute legacy emulator binary"));
#endif
}
