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
#include "machine.h"

#include <android/base/system/System.h>
#include <android/goldfish/config/hardware_config.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <memory>

#include "absl/log/globals.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"

#include "aemu/base/utils/status_matcher_macros.h"
#include "android/base/testing/TestSystem.h"
#include "android/cmdline-definitions.h"
#include "android/goldfish/config/emulator.h"
#include "mock_avd.h"

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;

namespace android::goldfish::test {

#ifdef __APPLE__
constexpr std::string_view bazelPostfix = ".signed";
#else
constexpr std::string_view bazelPostfix = "_std";
#endif

TEST(Machine, Basic_x86) {
    // TODO(b/400639867): Fix this:
    // Note that the following currently results in flaky tests due to the same tmp dir being used
    // by every test:
    //     base::TestSystem sys("");
    //     base::TestTempDir* tmp = sys.getTempRoot();
    //     ASSERT_TRUE(tmp->makeSubFile("qemu-system-x86_64_signed"));

    auto launcher_path = std::filesystem::temp_directory_path();
    // To satisfy System::findBundledExecutable.
    auto binary_path = launcher_path / absl::StrCat("qemu-system-x86_64", bazelPostfix);
    std::ofstream output(binary_path);

    base::TestSystem sys(launcher_path);

    auto hw = HardwareConfig();

    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();
    // First the 3 calls by Emulator ctor.
    EXPECT_CALL(*avd_ptr, name()).WillOnce(testing::Return("mock_avd"));
    EXPECT_CALL(*avd_ptr, hw()).WillOnce(testing::ReturnRef(hw));
    EXPECT_CALL(*avd_ptr, getIniFile()).WillOnce(testing::Return("some/path/mock_avd.ini"));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(2)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    AndroidOptions opts{};
    Emulator emu(std::move(avd), std::move(opts));

    Machine dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu), testing::ElementsAre(testing::StartsWith("-machine"),
                                                                 testing::StartsWith("goldfish,")));
}

TEST(Machine, Basic_arm64) {
    auto launcher_path = std::filesystem::temp_directory_path();
    // To satisfy System::findBundledExecutable.
    auto binary_path = launcher_path / absl::StrCat("qemu-system-aarch64", bazelPostfix);
    std::ofstream output(binary_path);

    base::TestSystem sys(launcher_path);

    auto hw = HardwareConfig();

    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();
    // First the 3 calls by Emulator ctor.
    EXPECT_CALL(*avd_ptr, name()).WillOnce(testing::Return("mock_avd"));
    EXPECT_CALL(*avd_ptr, hw()).WillOnce(testing::ReturnRef(hw));
    EXPECT_CALL(*avd_ptr, getIniFile()).WillOnce(testing::Return("some/path/mock_avd.ini"));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(2)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kArm));

    AndroidOptions opts{};
    Emulator emu(std::move(avd), std::move(opts));

    Machine dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                testing::ElementsAre(testing::StartsWith("-machine"),
                                     testing::StartsWith("goldfish-arm,")));
}

}  // namespace android::goldfish::test
