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

#include <filesystem>
#include <fstream>
#include <utility>

#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "gmock/gmock.h"

#include "android/base/system.h"
#include "android/base/testing/test_system.h"
#include "android/cmdline_definitions.h"
#include "android/cpu/cpu_accelerator.h"
#include "android/status/status_matcher_macros.h"
#include "cpu_device.h"
#include "fake_emulator.h"
#include "goldfish/file/file.h"

namespace android::goldfish::test {

TEST(Kernel, Basic_x86) {
    FakeEmulator emu;

    SystemImagePaths paths;
    paths.kernel_image = "some/path/kernel-ranchu";
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));

    EXPECT_CALL(emu.mock_avd(), Arch())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    CpuDevice cpu;
    KernelDevice dev(cpu);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(
            dev.getQemuParameters(emu.config()),
            testing::ElementsAre(testing::Eq("-kernel"), testing::Eq("some/path/kernel-ranchu"),
                                 testing::Eq("-append"),
                                 testing::AllOf(testing::HasSubstr("console=ttyS0,38400 "),
                                                testing::HasSubstr("clocksource=pit"),
                                                testing::HasSubstr("module_blacklist=test_meminit"),
                                                testing::HasSubstr("quiet"),
                                                testing::Not(testing::HasSubstr("earlyprintk")))));
}

TEST(Kernel, Basic_arm64) {
    FakeEmulator emu;

    SystemImagePaths paths;
    paths.kernel_image = "some/path/kernel-ranchu";
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));

    EXPECT_CALL(emu.mock_avd(), Arch())
            .Times(1)
            .WillRepeatedly(testing::Return(Avd::CpuArchitecture::kArm));

    CpuDevice cpu;
    KernelDevice dev(cpu);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(
            dev.getQemuParameters(emu.config()),
            testing::ElementsAre(testing::Eq("-kernel"), testing::Eq("some/path/kernel-ranchu"),
                                 testing::Eq("-append"),
                                 testing::AllOf(testing::HasSubstr("console=ttyAMA0,38400"),
                                                testing::HasSubstr("module_blacklist=test_meminit"),
                                                testing::HasSubstr("quiet"),
                                                testing::Not(testing::HasSubstr("earlyprintk")),
                                                testing::Not(testing::HasSubstr("keep_bootcon")))));
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

    CpuDevice cpu;
    KernelDevice dev(cpu);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(
                        testing::Eq("-kernel"), testing::Eq("some/path/kernel-ranchu"),
                        testing::Eq("-append"),
                        testing::AllOf(testing::HasSubstr("foo"), testing::HasSubstr("bar"),
                                       testing::HasSubstr("quiet"),
                                       testing::Not(testing::HasSubstr("earlyprintk")))));
}

TEST(Kernel, CommandLinePreservesEarlyprintkWhenShowKernel) {
    AndroidOptions opts{.show_kernel = true};
    FakeEmulator emu(std::move(opts));

    SystemImagePaths paths;
    paths.kernel_image = "some/path/kernel-ranchu";
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    CpuDevice cpu;
    KernelDevice dev(cpu);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(
                        testing::Eq("-kernel"), testing::Eq("some/path/kernel-ranchu"),
                        testing::Eq("-append"),
                        testing::AllOf(testing::HasSubstr("earlyprintk=ttyS0"),
                                       testing::HasSubstr("printk.devkmsg=on"),
                                       testing::Not(testing::HasSubstr("quiet")))));
}

TEST(Kernel, CommandLinePreservesEarlyprintkWhenShell) {
    AndroidOptions opts{.shell = true};
    FakeEmulator emu(std::move(opts));

    SystemImagePaths paths;
    paths.kernel_image = "some/path/kernel-ranchu";
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kArm));

    CpuDevice cpu;
    KernelDevice dev(cpu);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(
                        testing::Eq("-kernel"), testing::Eq("some/path/kernel-ranchu"),
                        testing::Eq("-append"),
                        testing::AllOf(testing::HasSubstr("earlyprintk=ttyAMA0"),
                                       testing::HasSubstr("keep_bootcon"),
                                       testing::HasSubstr("printk.devkmsg=on"),
                                       testing::Not(testing::HasSubstr("quiet")))));
}

TEST(Kernel, CommandLinePreservesEarlyprintkWhenShellSerial) {
    char shell_serial[] = "ttyS1";
    AndroidOptions opts{.shell_serial = shell_serial};
    FakeEmulator emu(std::move(opts));

    SystemImagePaths paths;
    paths.kernel_image = "some/path/kernel-ranchu";
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    CpuDevice cpu;
    KernelDevice dev(cpu);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(
                        testing::Eq("-kernel"), testing::Eq("some/path/kernel-ranchu"),
                        testing::Eq("-append"),
                        testing::AllOf(testing::HasSubstr("earlyprintk=ttyS0"),
                                       testing::HasSubstr("printk.devkmsg=on"),
                                       testing::Not(testing::HasSubstr("quiet")))));
}

TEST(Kernel, CommandLineMergesKernelCmdlineFile) {
    base::TestSystem sys("");
    auto non_ascii_dir = sys.GetTempRoot()->Path() / "测试_🚀_é";
    ASSERT_OK(base::file::mkdir_recursive(non_ascii_dir, 0755));
    auto cmdline_path = non_ascii_dir / "kernel_cmdline.txt";

    {
        std::ofstream ofs(cmdline_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << "test_custom_arg=42 other_flag\n";
    }

    FakeEmulator emu;
    SystemImagePaths paths;
    paths.kernel_image = "some/path/kernel-ranchu";
    paths.kernel_cmdline = cmdline_path;
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    CpuDevice cpu;
    KernelDevice dev(cpu);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(
                        testing::Eq("-kernel"), testing::Eq("some/path/kernel-ranchu"),
                        testing::Eq("-append"),
                        testing::AllOf(testing::HasSubstr("test_custom_arg=42"),
                                       testing::HasSubstr("other_flag"),
                                       testing::HasSubstr("quiet"),
                                       testing::Not(testing::HasSubstr("earlyprintk")))));
}

TEST(Kernel, CommandLinePreservesExplicitAppendEarlyprintk) {
    char append_val[] = "earlyprintk=custom";
    ParamList custom_append;
    custom_append.param = append_val;
    custom_append.next = nullptr;

    AndroidOptions opts{.append = &custom_append};
    FakeEmulator emu(std::move(opts));

    SystemImagePaths paths;
    paths.kernel_image = "some/path/kernel-ranchu";
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    CpuDevice cpu;
    KernelDevice dev(cpu);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(
                        testing::Eq("-kernel"), testing::Eq("some/path/kernel-ranchu"),
                        testing::Eq("-append"),
                        testing::AllOf(testing::HasSubstr("earlyprintk=custom"),
                                       testing::HasSubstr("quiet"))));
}

TEST(Kernel, X86WithKvmAddsTscReliable) {
    FakeEmulator emu;

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));

    SystemImagePaths paths;
    paths.kernel_image = "some/path/kernel-ranchu";
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    SetCurrentCpuAcceleratorForTesting(CPU_ACCELERATOR_KVM, ANDROID_CPU_ACCELERATION_READY, "");
    CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture::kX86);

    CpuDevice cpu;
    EXPECT_OK(cpu.initialize(emu.config()));
    EXPECT_EQ(cpu.accelerator(), "kvm");

    KernelDevice dev(cpu);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(testing::Eq("-kernel"), testing::Eq("some/path/kernel-ranchu"),
                                     testing::Eq("-append"),
                                     testing::HasSubstr("tsc=reliable")));
}

TEST(Kernel, X86WithTcgOverrideOmitsTscReliable) {
    android::base::System::SetEnvironmentVariable("ANDROID_I_WANT_MY_TCG", "yes");
    AndroidOptions opts{.accel = "off"};
    FakeEmulator emu(std::move(opts));

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));

    SystemImagePaths paths;
    paths.kernel_image = "some/path/kernel-ranchu";
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    SetCurrentCpuAcceleratorForTesting(CPU_ACCELERATOR_KVM, ANDROID_CPU_ACCELERATION_READY, "");
    CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture::kX86);

    CpuDevice cpu;
    EXPECT_OK(cpu.initialize(emu.config()));
    EXPECT_EQ(cpu.accelerator(), "tcg");

    KernelDevice dev(cpu);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(testing::Eq("-kernel"), testing::Eq("some/path/kernel-ranchu"),
                                     testing::Eq("-append"),
                                     testing::Not(testing::HasSubstr("tsc=reliable"))));

    android::base::System::SetEnvironmentVariable("ANDROID_I_WANT_MY_TCG", "");
}

TEST(Kernel, X86WithAehdOverrideOmitsTscReliable) {
    android::base::System::SetEnvironmentVariable("ENABLE_AEHD_IN_QEMU", "1");
    FakeEmulator emu;

    auto hw = HardwareConfig();
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(testing::ReturnRef(hw));

    SystemImagePaths paths;
    paths.kernel_image = "some/path/kernel-ranchu";
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    SetCurrentCpuAcceleratorForTesting(CPU_ACCELERATOR_KVM, ANDROID_CPU_ACCELERATION_READY, "");
    CpuDevice::forceHostArch_TestOnly(Avd::CpuArchitecture::kX86);

    CpuDevice cpu;
    EXPECT_OK(cpu.initialize(emu.config()));
    EXPECT_EQ(cpu.accelerator(), "aehd");

    KernelDevice dev(cpu);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(testing::Eq("-kernel"), testing::Eq("some/path/kernel-ranchu"),
                                     testing::Eq("-append"),
                                     testing::Not(testing::HasSubstr("tsc=reliable"))));

    android::base::System::SetEnvironmentVariable("ENABLE_AEHD_IN_QEMU", "");
}

TEST(Kernel, PrependsDefaultModuleBlacklistBeforeUserAppend) {
    char append_val[] = "module_blacklist=aaa";
    ParamList custom_append{.param = append_val, .next = nullptr};
    AndroidOptions opts{.append = &custom_append};
    FakeEmulator emu(std::move(opts));

    SystemImagePaths paths;
    paths.kernel_image = "some/path/kernel-ranchu";
    EXPECT_CALL(emu.mock_avd(), GetSystemImagePaths()).WillRepeatedly(testing::ReturnRef(paths));
    EXPECT_CALL(emu.mock_avd(), Arch()).WillRepeatedly(testing::Return(Avd::CpuArchitecture::kX86));

    CpuDevice cpu;
    KernelDevice dev(cpu);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                testing::ElementsAre(
                        testing::Eq("-kernel"), testing::Eq("some/path/kernel-ranchu"),
                        testing::Eq("-append"),
                        testing::AllOf(testing::StartsWith("module_blacklist=test_meminit "),
                                       testing::HasSubstr(" module_blacklist=aaa"))));
}

}  // namespace android::goldfish::test
