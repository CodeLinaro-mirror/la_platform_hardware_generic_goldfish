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
#include "cpu_device.h"

#include <android/base/system/System.h>
#include <android/goldfish/config/hardware_config.h>
#include <gtest/gtest.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"

#include "aemu/base/utils/status_matcher_macros.h"
#include "android/cmdline-definitions.h"
#include "android/goldfish/config/emulator.h"
#include "android/goldfish/cpu/CpuAccelerator.h"
#include "mock_avd.h"

namespace android::goldfish::test {

using ::absl_testing::StatusIs;

TEST(Cpu, Basic_x86) {
    auto hw = HardwareConfig();
    hw.hw_cpu_ncore = 3;

    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();
    EXPECT_CALL(*avd_ptr, hw()).Times(2).WillRepeatedly(testing::ReturnRef(hw));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    SetCurrentCpuAcceleratorForTesting(CpuAccelerator::CPU_ACCELERATOR_KVM, AndroidCpuAcceleration::ANDROID_CPU_ACCELERATION_READY, "");
    CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture::kX86);

    AndroidOptions opts{};
    Emulator emu(std::move(avd), std::move(opts));

    CpuDevice dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                testing::ElementsAre(testing::Eq("-smp"), testing::Eq("3"),
                                     testing::Eq("-cpu"), testing::Eq("SandyBridge"),
                                     testing::Eq("-accel"), testing::Eq("kvm")));
}

TEST(Cpu, Basic_arm64) {
    auto hw = HardwareConfig();
    hw.hw_cpu_ncore = 3;

    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();
    EXPECT_CALL(*avd_ptr, hw()).Times(2).WillRepeatedly(testing::ReturnRef(hw));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kArm));

    SetCurrentCpuAcceleratorForTesting(CpuAccelerator::CPU_ACCELERATOR_HVF, AndroidCpuAcceleration::ANDROID_CPU_ACCELERATION_READY, "");
    CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture::kArm);

    AndroidOptions opts{};
    Emulator emu(std::move(avd), std::move(opts));

    CpuDevice dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                testing::ElementsAre(testing::Eq("-smp"), testing::Eq("3"),
                                     testing::Eq("-cpu"), testing::Eq("cortex-a53"),
                                     testing::Eq("-accel"), testing::Eq("hvf")));
}

TEST(Cpu, NoAccel) {
    auto hw = HardwareConfig();
    hw.hw_cpu_ncore = 3;

    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();
    EXPECT_CALL(*avd_ptr, hw()).Times(2).WillRepeatedly(testing::ReturnRef(hw));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kArm));

    SetCurrentCpuAcceleratorForTesting(CpuAccelerator::CPU_ACCELERATOR_HVF, AndroidCpuAcceleration::ANDROID_CPU_ACCELERATION_READY, "");
    CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture::kArm);

    AndroidOptions opts{};
    opts.no_accel = true;

    Emulator emu(std::move(avd), std::move(opts));

    CpuDevice dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                testing::ElementsAre(testing::Eq("-smp"), testing::Eq("3"),
                                     testing::Eq("-cpu"), testing::Eq("cortex-a53"),
                                     testing::Eq("-accel"), testing::Eq("tcg")));
}

TEST(Cpu, AccelOff) {
    auto hw = HardwareConfig();
    hw.hw_cpu_ncore = 3;

    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();
    EXPECT_CALL(*avd_ptr, hw()).Times(2).WillRepeatedly(testing::ReturnRef(hw));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kArm));

    SetCurrentCpuAcceleratorForTesting(CpuAccelerator::CPU_ACCELERATOR_HVF, AndroidCpuAcceleration::ANDROID_CPU_ACCELERATION_READY, "");
    CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture::kArm);

    AndroidOptions opts{};
    opts.accel = "off";

    Emulator emu(std::move(avd), std::move(opts));

    CpuDevice dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                testing::ElementsAre(testing::Eq("-smp"), testing::Eq("3"),
                                     testing::Eq("-cpu"), testing::Eq("cortex-a53"),
                                     testing::Eq("-accel"), testing::Eq("tcg")));
}

TEST(Cpu, HostAndTargetMismatch) {
    auto hw = HardwareConfig();
    hw.hw_cpu_ncore = 3;

    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();
    EXPECT_CALL(*avd_ptr, hw()).Times(2).WillRepeatedly(testing::ReturnRef(hw));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    SetCurrentCpuAcceleratorForTesting(CpuAccelerator::CPU_ACCELERATOR_HVF, AndroidCpuAcceleration::ANDROID_CPU_ACCELERATION_READY, "");
    CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture::kArm);

    AndroidOptions opts{};
    Emulator emu(std::move(avd), std::move(opts));

    CpuDevice dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                testing::ElementsAre(testing::Eq("-smp"), testing::Eq("3"),
                                     testing::Eq("-cpu"), testing::Eq("SandyBridge"),
                                     testing::Eq("-accel"), testing::Eq("tcg")));
}

TEST(Cpu, CoresFlagOverride) {
    auto hw = HardwareConfig();
    hw.hw_cpu_ncore = 3;

    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();
    EXPECT_CALL(*avd_ptr, hw()).Times(2).WillRepeatedly(testing::ReturnRef(hw));

    EXPECT_CALL(*avd_ptr, detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    SetCurrentCpuAcceleratorForTesting(CpuAccelerator::CPU_ACCELERATOR_KVM, AndroidCpuAcceleration::ANDROID_CPU_ACCELERATION_READY, "");
    CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture::kX86);

    AndroidOptions opts{};
    opts.cores = "5";
    Emulator emu(std::move(avd), std::move(opts));

    CpuDevice dev;
    EXPECT_OK(dev.initialize(emu));
    EXPECT_THAT(dev.getQemuParameters(emu),
                testing::ElementsAre(testing::Eq("-smp"), testing::Eq("5"),
                                     testing::Eq("-cpu"), testing::Eq("SandyBridge"),
                                     testing::Eq("-accel"), testing::Eq("kvm")));
}

TEST(Cpu, CoresFlagInvalid) {
    auto hw = HardwareConfig();
    hw.hw_cpu_ncore = 3;

    auto avd = std::make_unique<MockAvd>();

    MockAvd* avd_ptr = avd.get();
    EXPECT_CALL(*avd_ptr, hw()).Times(1).WillRepeatedly(testing::ReturnRef(hw));

    AndroidOptions opts{};
    opts.cores = "nan";
    Emulator emu(std::move(avd), std::move(opts));

    CpuDevice dev;
    EXPECT_THAT(dev.initialize(emu), StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace android::goldfish::test
