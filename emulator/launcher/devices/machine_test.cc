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

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"

#include "aemu/base/utils/status_matcher_macros.h"
#include "android/base/testing/TestSystem.h"
#include "fake_emulator.h"

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;

namespace android::goldfish::test {

#ifdef __APPLE__
constexpr std::string_view bazelPostfix = ".signed";
#else
constexpr std::string_view bazelPostfix = "";
#endif

TEST(Machine, Basic_x86) {
    // TODO(b/400639867): Fix this:
    // Note that the following currently results in flaky tests due to the same tmp dir being used
    // by every test:
    //     base::TestSystem sys("");
    //     base::TestTempDir* tmp = sys.getTempRoot();
    //     ASSERT_TRUE(tmp->makeSubFile("qemu-system-x86_64_signed"));

    auto launcher_path = std::filesystem::temp_directory_path();

    base::TestSystem sys(launcher_path);

    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), DetectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    Machine dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(
            dev.getQemuParameters(emu.config()),
            testing::ElementsAre(testing::StartsWith("-machine"), testing::StartsWith("goldfish")));
}

TEST(Machine, Basic_arm64) {
    auto launcher_path = std::filesystem::temp_directory_path();

    base::TestSystem sys(launcher_path);

    FakeEmulator emu;
    EXPECT_CALL(emu.mock_avd(), DetectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kArm));

    Machine dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(testing::StartsWith("-machine"),
                                     testing::StartsWith("goldfish-arm")));
}

}  // namespace android::goldfish::test
