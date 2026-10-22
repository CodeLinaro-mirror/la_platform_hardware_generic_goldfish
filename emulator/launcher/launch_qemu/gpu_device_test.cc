// Copyright 2026 The Android Open Source Project
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

#include "gpu_device.h"

#include <gtest/gtest.h>

#include "gmock/gmock.h"

#include "android/base/testing/test_system.h"
#include "android/cmdline_definitions.h"
#include "android/status/status_matcher_macros.h"
#include "fake_emulator.h"
#include "snapshot_device.h"

namespace android::goldfish::test {

using ::testing::Pair;
using ::testing::ReturnRef;
using ::testing::UnorderedElementsAre;

TEST(GpuDeviceTest, DefaultSoftwareGpuEnv) {
    base::TestSystem sys("");
    FakeEmulator emu;
    emu.emulator_paths().launcher_directory = "/default/launcher";
    HardwareConfig hw;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));

    SnapshotDevice snapshot_dev;
    GpuDevice dev("gpu0", snapshot_dev);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_EQ(dev.getVulkanIcd(), "lavapipe");
    EXPECT_THAT(dev.getQemuEnv(emu.config()),
                UnorderedElementsAre(Pair("ANDROID_EMU_RENDERER", "lavapipe"),
                                     Pair("ANDROID_EMU_VK_ICD", "lavapipe"),
                                     Pair("ANDROID_EGL_ON_EGL", "1"),
                                     Pair("ANGLE_DEFAULT_PLATFORM", "vulkan"),
                                     Pair("ANDROID_EMULATOR_LAUNCHER_DIR", "/default/launcher")));
}

TEST(GpuDeviceTest, HostGpuModeAndLauncherDirEnv) {
    base::TestSystem sys("");

    char gpu_mode[] = "host";
    AndroidOptions opts{.gpu = gpu_mode};
    FakeEmulator emu(opts);
    emu.emulator_paths().launcher_directory = "/path/to/launcher";
    HardwareConfig hw;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));

    SnapshotDevice snapshot_dev;
    GpuDevice dev("gpu0", snapshot_dev);
    EXPECT_OK(dev.initialize(emu.config()));

#if defined(__APPLE__)
    EXPECT_EQ(dev.getVulkanIcd(), "kosmickrisp");
    EXPECT_THAT(dev.getQemuEnv(emu.config()),
                UnorderedElementsAre(Pair("ANDROID_EMU_RENDERER", "host"),
                                     Pair("ANDROID_EMU_VK_ICD", "kosmickrisp"),
                                     Pair("ANDROID_EGL_ON_EGL", "1"),
                                     Pair("ANGLE_DEFAULT_PLATFORM", "vulkan"),
                                     Pair("ANDROID_EMULATOR_LAUNCHER_DIR", "/path/to/launcher")));
#else
    EXPECT_EQ(dev.getVulkanIcd(), "");
    EXPECT_THAT(dev.getQemuEnv(emu.config()),
                UnorderedElementsAre(
                        Pair("ANDROID_EMU_RENDERER", "host"), Pair("ANDROID_EMU_VK_ICD", ""),
                        Pair("ANDROID_EGL_ON_EGL", "1"), Pair("ANGLE_DEFAULT_PLATFORM", "vulkan"),
                        Pair("ANDROID_EMULATOR_LAUNCHER_DIR", "/path/to/launcher")));
#endif
}

#if defined(__APPLE__)
TEST(GpuDeviceTest, MacOsMoltenVkSelection) {
    base::TestSystem sys("");
    sys.EnvSet("ANDROID_EMU_VK_SELECT_ICD", "moltenvk");

    char gpu_mode[] = "host";
    AndroidOptions opts{.gpu = gpu_mode};
    FakeEmulator emu(opts);
    emu.emulator_paths().launcher_directory = "/path/to/launcher";
    HardwareConfig hw;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));

    SnapshotDevice snapshot_dev;
    GpuDevice dev("gpu0", snapshot_dev);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_EQ(dev.getVulkanIcd(), "moltenvk");
    EXPECT_THAT(dev.getQemuEnv(emu.config()),
                UnorderedElementsAre(Pair("ANDROID_EMU_RENDERER", "host"),
                                     Pair("ANDROID_EMU_VK_ICD", "moltenvk"),
                                     Pair("ANDROID_EGL_ON_EGL", "1"),
                                     Pair("ANGLE_DEFAULT_PLATFORM", "vulkan"),
                                     Pair("ANDROID_EMULATOR_LAUNCHER_DIR", "/path/to/launcher")));
}
#endif

#ifdef __linux__
TEST(GpuDeviceTest, LinuxPreloadLibgccEnv) {
    base::TestSystem sys("");
    sys.EnvSet("ANDROID_EMU_PRELOAD_LIBGCC", "1");

    FakeEmulator emu;
    emu.emulator_paths().launcher_directory = "/path/to/launcher";
    HardwareConfig hw;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));

    SnapshotDevice snapshot_dev;
    GpuDevice dev("gpu0", snapshot_dev);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuEnv(emu.config()),
                UnorderedElementsAre(Pair("ANDROID_EMU_RENDERER", "lavapipe"),
                                     Pair("ANDROID_EMU_VK_ICD", "lavapipe"),
                                     Pair("ANDROID_EGL_ON_EGL", "1"),
                                     Pair("ANGLE_DEFAULT_PLATFORM", "vulkan"),
                                     Pair("ANDROID_EMULATOR_LAUNCHER_DIR", "/path/to/launcher"),
                                     Pair("LD_PRELOAD", "/lib/x86_64-linux-gnu/libgcc_s.so.1")));

    sys.EnvSet("LD_PRELOAD", "/custom/preload.so");
    EXPECT_THAT(
            dev.getQemuEnv(emu.config()),
            UnorderedElementsAre(
                    Pair("ANDROID_EMU_RENDERER", "lavapipe"),
                    Pair("ANDROID_EMU_VK_ICD", "lavapipe"), Pair("ANDROID_EGL_ON_EGL", "1"),
                    Pair("ANGLE_DEFAULT_PLATFORM", "vulkan"),
                    Pair("ANDROID_EMULATOR_LAUNCHER_DIR", "/path/to/launcher"),
                    Pair("LD_PRELOAD", "/lib/x86_64-linux-gnu/libgcc_s.so.1:/custom/preload.so")));
}
#endif

}  // namespace android::goldfish::test
