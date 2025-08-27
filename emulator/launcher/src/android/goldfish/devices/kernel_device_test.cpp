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

#include <android/base/system/System.h>
#include <android/goldfish/config/hardware_config.h>
#include <gtest/gtest.h>

#include <memory>

#include "absl/status/status.h"
#include "gmock/gmock.h"

#include "aemu/base/utils/status_matcher_macros.h"
#include "android/cmdline-definitions.h"
#include "android/goldfish/config/emulator.h"
#include "mock_avd.h"

namespace android::goldfish::test {

TEST(Kernel, Basic_x86) {
    auto hw = HardwareConfig();

    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();
    EXPECT_CALL(*avd_ptr, hw()).Times(1).WillRepeatedly(testing::ReturnRef(hw));

    EXPECT_CALL(*avd_ptr, getSystemImageFilePath(Avd::ImageType::KERNELRANCHU))
            .WillOnce(testing::Return(absl::NotFoundError("not found")));
    EXPECT_CALL(*avd_ptr, getSystemImageFilePath(Avd::ImageType::KERNELRANCHU64))
            .WillOnce(testing::Return(absl::NotFoundError("not found")));
    EXPECT_CALL(*avd_ptr, getSystemImageFilePath(Avd::ImageType::KERNEL))
            .WillOnce(testing::Return("some/path/kernel-ranchu"));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    AndroidOptions opts{};
    Emulator emu(std::move(avd), std::move(opts));

    KernelDevice dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                testing::ElementsAre(testing::Eq("-kernel"), testing::Eq("some/path/kernel-ranchu"),
                                     testing::Eq("-append"), testing::HasSubstr("console=0 ")));
}

TEST(Kernel, Basic_arm64) {
    auto hw = HardwareConfig();

    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();
    EXPECT_CALL(*avd_ptr, hw()).Times(1).WillRepeatedly(testing::ReturnRef(hw));

    EXPECT_CALL(*avd_ptr, getSystemImageFilePath(Avd::ImageType::KERNELRANCHU))
            .WillOnce(testing::Return(absl::NotFoundError("not found")));
    EXPECT_CALL(*avd_ptr, getSystemImageFilePath(Avd::ImageType::KERNELRANCHU64))
            .WillOnce(testing::Return(absl::NotFoundError("not found")));
    EXPECT_CALL(*avd_ptr, getSystemImageFilePath(Avd::ImageType::KERNEL))
            .WillOnce(testing::Return("some/path/kernel-ranchu"));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kArm));

    AndroidOptions opts{};
    Emulator emu(std::move(avd), std::move(opts));

    KernelDevice dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(
            dev.getQemuParameters(emu),
            testing::ElementsAre(testing::Eq("-kernel"), testing::Eq("some/path/kernel-ranchu"),
                                 testing::Eq("-append"), testing::HasSubstr("console=ttyAMA0,")));
}

TEST(Kernel, AppendExtras) {
    auto hw = HardwareConfig();

    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();
    EXPECT_CALL(*avd_ptr, hw()).Times(1).WillRepeatedly(testing::ReturnRef(hw));

    EXPECT_CALL(*avd_ptr, getSystemImageFilePath(Avd::ImageType::KERNELRANCHU))
            .WillOnce(testing::Return("some/path/kernel-ranchu"));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    ParamList foo, bar;
    foo.param = "foo";
    foo.next = &bar;
    bar.param = "bar";
    bar.next = nullptr;

    AndroidOptions opts{.append = &foo};
    Emulator emu(std::move(avd), std::move(opts));

    KernelDevice dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                testing::ElementsAre(testing::Eq("-kernel"), testing::Eq("some/path/kernel-ranchu"),
                                     testing::Eq("-append"),
#ifdef _WIN32
                                     testing::EndsWith(" foo bar\"")));
#else
                                     testing::EndsWith(" foo bar'")));
#endif
}

}  // namespace android::goldfish::test
