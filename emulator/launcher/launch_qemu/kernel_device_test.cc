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

#include "kernel_device.h"

#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "gmock/gmock.h"

#include "android/cmdline_definitions.h"
#include "android/status/status_matcher_macros.h"
#include "fake_emulator.h"

namespace android::goldfish::test {

TEST(Kernel, Basic_x86) {
    FakeEmulator emu;

    SystemImagePaths paths;
    paths.kernel_image = "some/path/kernel-ranchu";
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));

    EXPECT_CALL(emu.mock_avd(), Arch())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    KernelDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(testing::Eq("-kernel"), testing::Eq("some/path/kernel-ranchu"),
                                     testing::Eq("-append"),
                                     testing::HasSubstr("console=ttyS0,38400 ")));
}

TEST(Kernel, Basic_arm64) {
    FakeEmulator emu;

    SystemImagePaths paths;
    paths.kernel_image = "some/path/kernel-ranchu";
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));

    EXPECT_CALL(emu.mock_avd(), Arch())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kArm));

    KernelDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(testing::Eq("-kernel"), testing::Eq("some/path/kernel-ranchu"),
                                     testing::Eq("-append"),
                                     testing::HasSubstr("console=ttyAMA0,38400")));
}

TEST(Kernel, AppendExtras) {
    ParamList foo, bar;
    foo.param = "foo";
    foo.next = &bar;
    bar.param = "bar";
    bar.next = nullptr;

    AndroidOptions opts{.append = &foo};
    FakeEmulator emu(std::move(opts));

    SystemImagePaths paths;
    paths.kernel_image = "some/path/kernel-ranchu";
    paths.kernel_cmdline = "some/path/kernel-ranchu-command.txt";
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));

    EXPECT_CALL(emu.mock_avd(), Arch())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    KernelDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(
                        testing::Eq("-kernel"), testing::Eq("some/path/kernel-ranchu"),
                        testing::Eq("-append"),
                        testing::AllOf(testing::HasSubstr("foo"), testing::HasSubstr("bar"))));
}

}  // namespace android::goldfish::test
