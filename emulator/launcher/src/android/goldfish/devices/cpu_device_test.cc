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

#include "emulator/launcher/src/android/goldfish/devices/cpu_device.h"

#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"

#include "aemu/base/utils/status_matcher_macros.h"
#include "android/cmdline_definitions.h"
#include "android/cpu_accelerator.h"
#include "emulator/launcher/src/android/goldfish/devices/fake_emulator.h"

namespace android::goldfish::test {

using ::absl_testing::StatusIs;

TEST(Cpu, Basic_x86) {
    FakeEmulator emu;

    auto hw = HardwareConfig();
    hw.hw_cpu_ncore = 3;

    EXPECT_CALL(emu.mock_avd(), hw()).Times(1).WillRepeatedly(testing::ReturnRef(hw));

    EXPECT_CALL(emu.mock_avd(), detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    SetCurrentCpuAcceleratorForTesting(CpuAccelerator::CPU_ACCELERATOR_KVM,
                                       AndroidCpuAcceleration::ANDROID_CPU_ACCELERATION_READY, "");
    CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture::kX86);

    CpuDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(testing::Eq("-smp"), testing::Eq("3"), testing::Eq("-cpu"),
                                     testing::Eq("SandyBridge"), testing::Eq("-accel"),
                                     testing::Eq("kvm")));
}

TEST(Cpu, Basic_arm64) {
    FakeEmulator emu;

    auto hw = HardwareConfig();
    hw.hw_cpu_ncore = 3;
    EXPECT_CALL(emu.mock_avd(), hw()).Times(1).WillRepeatedly(testing::ReturnRef(hw));

    EXPECT_CALL(emu.mock_avd(), detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kArm));

    SetCurrentCpuAcceleratorForTesting(CpuAccelerator::CPU_ACCELERATOR_HVF,
                                       AndroidCpuAcceleration::ANDROID_CPU_ACCELERATION_READY, "");
    CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture::kArm);

    CpuDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(testing::Eq("-smp"), testing::Eq("3"), testing::Eq("-cpu"),
                                     testing::Eq("cortex-a53"), testing::Eq("-accel"),
                                     testing::Eq("hvf")));
}

TEST(Cpu, NoAccel) {
    AndroidOptions opts{.no_accel = true};
    FakeEmulator emu(std::move(opts));

    auto hw = HardwareConfig();
    hw.hw_cpu_ncore = 3;
    EXPECT_CALL(emu.mock_avd(), hw()).Times(1).WillRepeatedly(testing::ReturnRef(hw));

    EXPECT_CALL(emu.mock_avd(), detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kArm));

    SetCurrentCpuAcceleratorForTesting(CpuAccelerator::CPU_ACCELERATOR_HVF,
                                       AndroidCpuAcceleration::ANDROID_CPU_ACCELERATION_READY, "");
    CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture::kArm);

    CpuDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(testing::Eq("-smp"), testing::Eq("3"), testing::Eq("-cpu"),
                                     testing::Eq("cortex-a53"), testing::Eq("-accel"),
                                     testing::Eq("tcg")));
}

TEST(Cpu, AccelOff) {
    AndroidOptions opts{.accel = "off"};
    FakeEmulator emu(std::move(opts));

    auto hw = HardwareConfig();
    hw.hw_cpu_ncore = 3;
    EXPECT_CALL(emu.mock_avd(), hw()).Times(1).WillRepeatedly(testing::ReturnRef(hw));

    EXPECT_CALL(emu.mock_avd(), detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kArm));

    SetCurrentCpuAcceleratorForTesting(CpuAccelerator::CPU_ACCELERATOR_HVF,
                                       AndroidCpuAcceleration::ANDROID_CPU_ACCELERATION_READY, "");
    CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture::kArm);

    CpuDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(testing::Eq("-smp"), testing::Eq("3"), testing::Eq("-cpu"),
                                     testing::Eq("cortex-a53"), testing::Eq("-accel"),
                                     testing::Eq("tcg")));
}

TEST(Cpu, HostAndTargetMismatch) {
    FakeEmulator emu;

    auto hw = HardwareConfig();
    hw.hw_cpu_ncore = 3;
    EXPECT_CALL(emu.mock_avd(), hw()).Times(0).WillRepeatedly(testing::ReturnRef(hw));

    EXPECT_CALL(emu.mock_avd(), detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    SetCurrentCpuAcceleratorForTesting(CpuAccelerator::CPU_ACCELERATOR_HVF,
                                       AndroidCpuAcceleration::ANDROID_CPU_ACCELERATION_READY, "");
    CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture::kArm);

    CpuDevice dev;
    EXPECT_THAT(dev.initialize(emu.config()),
                absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(Cpu, NoHardwareAcceleratorAvailable) {
    FakeEmulator emu;

    auto hw = HardwareConfig();
    hw.hw_cpu_ncore = 3;
    EXPECT_CALL(emu.mock_avd(), hw()).Times(0).WillRepeatedly(testing::ReturnRef(hw));

    EXPECT_CALL(emu.mock_avd(), detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    SetCurrentCpuAcceleratorForTesting(
            CpuAccelerator::CPU_ACCELERATOR_NONE,
            AndroidCpuAcceleration::ANDROID_CPU_ACCELERATION_ACCEL_NOT_INSTALLED, "");
    CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture::kX86);

    CpuDevice dev;
    EXPECT_THAT(dev.initialize(emu.config()),
                absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(Cpu, CoresFlagOverride) {
    AndroidOptions opts{.cores = "5"};
    FakeEmulator emu(std::move(opts));

    auto hw = HardwareConfig();
    hw.hw_cpu_ncore = 3;
    EXPECT_CALL(emu.mock_avd(), hw()).Times(1).WillRepeatedly(testing::ReturnRef(hw));

    EXPECT_CALL(emu.mock_avd(), detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    SetCurrentCpuAcceleratorForTesting(CpuAccelerator::CPU_ACCELERATOR_KVM,
                                       AndroidCpuAcceleration::ANDROID_CPU_ACCELERATION_READY, "");
    CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture::kX86);

    CpuDevice dev;
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(testing::Eq("-smp"), testing::Eq("5"), testing::Eq("-cpu"),
                                     testing::Eq("SandyBridge"), testing::Eq("-accel"),
                                     testing::Eq("kvm")));
}

TEST(Cpu, CoresFlagInvalid) {
    AndroidOptions opts{.cores = "nan"};
    FakeEmulator emu(std::move(opts));

    auto hw = HardwareConfig();
    hw.hw_cpu_ncore = 3;
    EXPECT_CALL(emu.mock_avd(), hw()).Times(1).WillRepeatedly(testing::ReturnRef(hw));

    EXPECT_CALL(emu.mock_avd(), detectArchitecture())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    SetCurrentCpuAcceleratorForTesting(CpuAccelerator::CPU_ACCELERATOR_KVM,
                                       AndroidCpuAcceleration::ANDROID_CPU_ACCELERATION_READY, "");
    CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture::kX86);

    CpuDevice dev;
    EXPECT_THAT(dev.initialize(emu.config()), StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace android::goldfish::test
