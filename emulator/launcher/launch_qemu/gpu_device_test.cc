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

#include <cstring>
#include <string>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"

#include "android/base/testing/test_system.h"
#include "android/cmdline_definitions.h"
#include "android/status/status_matcher_macros.h"
#include "fake_emulator.h"
#include "goldfish/file/file.h"
#include "snapshot_device.h"

namespace android::goldfish::test {

using ::absl::ScopedMockLog;
using ::testing::_;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::Pair;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StartsWith;
using ::testing::UnorderedElementsAre;

namespace {

// On Linux GpuDevice::getQemuEnv() always adds GLIBC_TUNABLES (static TLS for
// the bundled Mesa, see LinuxStaticTlsTunablesEnv); tests that check the rest
// of the environment on every platform strip it here.
absl::flat_hash_map<std::string, std::string> EnvWithoutGlibcTunables(
        absl::flat_hash_map<std::string, std::string> env) {
    env.erase("GLIBC_TUNABLES");
    return env;
}

}  // namespace
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
    EXPECT_THAT(EnvWithoutGlibcTunables(dev.getQemuEnv(emu.config())),
                UnorderedElementsAre(Pair("ANDROID_EMU_RENDERER", "lavapipe"),
                                     Pair("ANDROID_EMU_VK_ICD", "lavapipe"),
                                     Pair("ANDROID_EGL_ON_EGL", "1"),
                                     Pair("ANGLE_DEFAULT_PLATFORM", "vulkan"),
                                     Pair("ANDROID_EMULATOR_LAUNCHER_DIR", "/default/launcher")));
}

TEST(GpuDeviceTest, EmitsVirtioGpuGfxstreamDevice) {
    base::TestSystem sys("");
    FakeEmulator emu;
    HardwareConfig hw;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));

    ScopedMockLog log(absl::MockLogDefault::kIgnoreUnexpected);
    EXPECT_CALL(log, Log(absl::LogSeverity::kInfo, _,
                         HasSubstr("Using virtio-gpu device: virtio-gpu-gfxstream")));
    log.StartCapturingLogs();
    SnapshotDevice snapshot_dev;
    GpuDevice dev("gpu0", snapshot_dev);
    EXPECT_OK(dev.initialize(emu.config()));
    log.StopCapturingLogs();
    const std::vector<std::string> params = dev.getQemuParameters(emu.config());
    ASSERT_THAT(params, ElementsAre(Eq("-device"), HasSubstr("gfxstream-vulkan=on")));
    EXPECT_THAT(params[1], StartsWith("virtio-gpu-gfxstream,"));
}

TEST(GpuDeviceTest, EscapesCommasInSnapshotDirectoryAndRendererFeatures) {
    base::TestSystem sys("");
    const fs::path avd_content = sys.GetTempRoot()->Path() / "avd,with,comma";
    ASSERT_OK(base::file::mkdir_recursive(avd_content, 0755));

    char renderer_features[] = "FeatureA:enabled,FeatureB:disabled";
    AndroidOptions opts{.renderer_features = renderer_features};
    FakeEmulator emu(opts);
    HardwareConfig hw;
    hw.hw_lcd_width = 1080;
    hw.hw_lcd_height = 2400;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(avd_content));

    SnapshotDevice snapshot_dev;
    EXPECT_OK(snapshot_dev.initialize(emu.config()));
    ASSERT_TRUE(snapshot_dev.is_enabled());

    GpuDevice dev("gpu0", snapshot_dev);
    EXPECT_OK(dev.initialize(emu.config()));

    const fs::path expected_snapshot_dir = avd_content / "snapshots" / "";
    EXPECT_THAT(
            dev.getQemuParameters(emu.config()),
            ElementsAre(Eq("-device"),
                        AllOf(HasSubstr(";FeatureA:enabled,,FeatureB:disabled"),
                              HasSubstr(absl::StrCat("snapshot_directory=",
                                                     Device::EscapeQemuOptionValue(
                                                             expected_snapshot_dir.string()))))));
}

TEST(GpuDeviceTest, SnapshotDirectoryPresentWhenOnlyQuickbootIsOff) {
    base::TestSystem sys("");
    const fs::path avd_content = sys.GetTempRoot()->Path() / "avd";
    ASSERT_OK(base::file::mkdir_recursive(avd_content, 0755));

    // Runtime snapshots (gRPC, console) stay available with -no-snapshot-load
    // -no-snapshot-save, so the GPU still needs its snapshot directory.
    AndroidOptions opts{};
    opts.no_snapshot_load = true;
    opts.no_snapshot_save = true;
    FakeEmulator emu(opts);
    HardwareConfig hw;
    hw.hw_lcd_width = 1080;
    hw.hw_lcd_height = 2400;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(avd_content));

    SnapshotDevice snapshot_dev;
    EXPECT_OK(snapshot_dev.initialize(emu.config()));
    ASSERT_FALSE(snapshot_dev.is_enabled());
    ASSERT_TRUE(snapshot_dev.snapshots_enabled());

    GpuDevice dev("gpu0", snapshot_dev);
    EXPECT_OK(dev.initialize(emu.config()));

    const fs::path expected_snapshot_dir = avd_content / "snapshots" / "";
    EXPECT_THAT(
            dev.getQemuParameters(emu.config()),
            ElementsAre(Eq("-device"),
                        AllOf(HasSubstr("VulkanSnapshots:enabled"),
                              HasSubstr(absl::StrCat("snapshot_directory=",
                                                     Device::EscapeQemuOptionValue(
                                                             expected_snapshot_dir.string()))))));
}

TEST(GpuDeviceTest, NoSnapshotDisablesSnapshotDirectory) {
    base::TestSystem sys("");
    const fs::path avd_content = sys.GetTempRoot()->Path() / "avd";
    ASSERT_OK(base::file::mkdir_recursive(avd_content, 0755));

    AndroidOptions opts{};
    opts.no_snapshot = true;
    FakeEmulator emu(opts);
    HardwareConfig hw;
    hw.hw_lcd_width = 1080;
    hw.hw_lcd_height = 2400;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(avd_content));

    SnapshotDevice snapshot_dev;
    EXPECT_OK(snapshot_dev.initialize(emu.config()));
    ASSERT_FALSE(snapshot_dev.snapshots_enabled());

    GpuDevice dev("gpu0", snapshot_dev);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuParameters(emu.config()),
                ElementsAre(Eq("-device"), AllOf(Not(HasSubstr("VulkanSnapshots:enabled")),
                                                 Not(HasSubstr("snapshot_directory=")))));
}

TEST(GpuDeviceTest, HostGpuModeAndLauncherDirEnv) {
    base::TestSystem sys("");

    FakeEmulator emu;
    emu.emulator_paths().launcher_directory = "/path/to/launcher";
    emu.gpu_selection() = {
        .chosen_gpu_mode = "host",
        .emu_renderer = "host",
#if defined(__APPLE__)
        .vulkan_icd = "kosmickrisp",
#else
        .vulkan_icd = "",
#endif
    };
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

    sys.EnvSet("ANDROID_EMU_VK_SELECT_GPU", "1");
    EXPECT_THAT(dev.getQemuEnv(emu.config()),
                UnorderedElementsAre(Pair("ANDROID_EMU_RENDERER", "host"),
                                     Pair("ANDROID_EMU_VK_ICD", "kosmickrisp"),
                                     Pair("ANDROID_EGL_ON_EGL", "1"),
                                     Pair("ANGLE_DEFAULT_PLATFORM", "vulkan"),
                                     Pair("ANDROID_EMULATOR_LAUNCHER_DIR", "/path/to/launcher"),
                                     Pair("ANDROID_EMU_VK_SELECT_GPU", "1")));
#else
    EXPECT_EQ(dev.getVulkanIcd(), "");
    EXPECT_THAT(EnvWithoutGlibcTunables(dev.getQemuEnv(emu.config())),
                UnorderedElementsAre(
                        Pair("ANDROID_EMU_RENDERER", "host"), Pair("ANDROID_EMU_VK_ICD", ""),
                        Pair("ANDROID_EGL_ON_EGL", "1"), Pair("ANGLE_DEFAULT_PLATFORM", "vulkan"),
                        Pair("ANDROID_EMULATOR_LAUNCHER_DIR", "/path/to/launcher")));

    sys.EnvSet("ANDROID_EMU_VK_SELECT_GPU", "1");
    EXPECT_THAT(EnvWithoutGlibcTunables(dev.getQemuEnv(emu.config())),
                UnorderedElementsAre(
                        Pair("ANDROID_EMU_RENDERER", "host"), Pair("ANDROID_EMU_VK_ICD", ""),
                        Pair("ANDROID_EGL_ON_EGL", "1"), Pair("ANGLE_DEFAULT_PLATFORM", "vulkan"),
                        Pair("ANDROID_EMULATOR_LAUNCHER_DIR", "/path/to/launcher"),
                        Pair("ANDROID_EMU_VK_SELECT_GPU", "1")));
#endif
}

#ifdef __linux__
constexpr char kStaticTlsTunable[] = "glibc.rtld.optional_static_tls=16384";

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
                                     Pair("GLIBC_TUNABLES", kStaticTlsTunable),
                                     Pair("LD_PRELOAD", "/lib/x86_64-linux-gnu/libgcc_s.so.1")));

    sys.EnvSet("LD_PRELOAD", "/custom/preload.so");
    EXPECT_THAT(
            dev.getQemuEnv(emu.config()),
            UnorderedElementsAre(
                    Pair("ANDROID_EMU_RENDERER", "lavapipe"),
                    Pair("ANDROID_EMU_VK_ICD", "lavapipe"), Pair("ANDROID_EGL_ON_EGL", "1"),
                    Pair("ANGLE_DEFAULT_PLATFORM", "vulkan"),
                    Pair("ANDROID_EMULATOR_LAUNCHER_DIR", "/path/to/launcher"),
                    Pair("GLIBC_TUNABLES", kStaticTlsTunable),
                    Pair("LD_PRELOAD", "/lib/x86_64-linux-gnu/libgcc_s.so.1:/custom/preload.so")));
}

// The bundled Mesa GLES libraries need more static TLS than glibc reserves for
// dlopen()ed libraries; the launcher raises the limit without clobbering a
// GLIBC_TUNABLES the user already set.
TEST(GpuDeviceTest, LinuxStaticTlsTunablesEnv) {
    base::TestSystem sys("");
    FakeEmulator emu;
    HardwareConfig hw;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));

    SnapshotDevice snapshot_dev;
    GpuDevice dev("gpu0", snapshot_dev);
    EXPECT_OK(dev.initialize(emu.config()));
    EXPECT_THAT(dev.getQemuEnv(emu.config()),
                testing::Contains(Pair("GLIBC_TUNABLES", kStaticTlsTunable)));

    sys.EnvSet("GLIBC_TUNABLES", "glibc.malloc.tcache_count=0");
    EXPECT_THAT(
            dev.getQemuEnv(emu.config()),
            testing::Contains(Pair("GLIBC_TUNABLES", absl::StrCat("glibc.malloc.tcache_count=0:",
                                                                  kStaticTlsTunable))));

    sys.EnvSet("GLIBC_TUNABLES", "glibc.rtld.optional_static_tls=65536");
    EXPECT_THAT(dev.getQemuEnv(emu.config()),
                testing::Contains(Pair("GLIBC_TUNABLES", "glibc.rtld.optional_static_tls=65536")));
}
#endif

TEST(GpuDeviceTest, RendererFeaturesRespectFeatureFlags) {
    using S = android_studio::EmulatorFeatureFlagState;
    base::TestSystem sys("");
    const fs::path avd_content = sys.GetTempRoot()->Path() / "avd";
    ASSERT_OK(base::file::mkdir_recursive(avd_content, 0755));

    HardwareConfig hw;
    hw.hw_lcd_width = 1080;
    hw.hw_lcd_height = 2400;

    // Default flags: GuestVulkanOnly, VulkanNativeSwapchain, VulkanVirtualQueue,
    // VulkanSnapshots, VulkanUseDedicatedAhbMemoryType enabled;
    // VulkanBatchedDescriptorSetUpdate disabled.
    {
        FakeEmulator emu;
        EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));
        EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(avd_content));

        SnapshotDevice snapshot_dev;
        EXPECT_OK(snapshot_dev.initialize(emu.config()));
        GpuDevice dev("gpu0", snapshot_dev);
        EXPECT_OK(dev.initialize(emu.config()));

        EXPECT_THAT(dev.getQemuParameters(emu.config()),
                    ElementsAre(Eq("-device"),
                                AllOf(HasSubstr("GuestVulkanOnly:enabled"),
                                      HasSubstr("VulkanNativeSwapchain:enabled"),
                                      HasSubstr("VulkanVirtualQueue:enabled"),
                                      HasSubstr("VulkanSnapshots:enabled"),
                                      HasSubstr("VulkanBatchedDescriptorSetUpdate:disabled"),
                                      HasSubstr("VulkanUseDedicatedAhbMemoryType:enabled"),
                                      testing::Not(HasSubstr("x-gfxstream-gles=on")))));
    }

    // Disabling GuestAngle, VulkanVirtualQueue, VulkanSnapshots, VulkanUseDedicatedAhbMemoryType
    // and enabling VulkanBatchedDescriptorSetUpdate + optional Vulkan debug/memory flags.
    {
        FakeEmulator emu;
        emu.feature_flags().SetEnabledOverride(S::GUEST_ANGLE, false);
        emu.feature_flags().SetEnabledOverride(S::VULKAN_VIRTUAL_QUEUE, false);
        emu.feature_flags().SetEnabledOverride(S::VULKAN_SNAPSHOTS, false);
        emu.feature_flags().SetEnabledOverride(S::VULKAN_USE_DEDICATED_AHB_MEMORY_TYPE, false);
        emu.feature_flags().SetEnabledOverride(S::VULKAN_BATCHED_DESCRIPTOR_SET_UPDATE, true);
        emu.feature_flags().SetEnabledOverride(S::BYPASS_VULKAN_DEVICE_FEATURE_OVERRIDES, true);
        emu.feature_flags().SetEnabledOverride(S::VULKAN_ALLOCATE_DEVICE_MEMORY_ONLY, true);
        emu.feature_flags().SetEnabledOverride(S::VULKAN_ALLOCATE_HOST_MEMORY, true);
        emu.feature_flags().SetEnabledOverride(S::VULKAN_DEBUG_UTILS, true);
        emu.feature_flags().SetEnabledOverride(S::VULKAN_COMMAND_BUFFER_CHECKPOINTS, true);
        emu.feature_flags().SetEnabledOverride(S::VULKAN_ROBUSTNESS, true);
        emu.feature_flags().SetEnabledOverride(S::VULKAN_PROTECTED_MEMORY_EMULATION, true);
        EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));
        EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(avd_content));

        SnapshotDevice snapshot_dev;
        EXPECT_OK(snapshot_dev.initialize(emu.config()));
        GpuDevice dev("gpu0", snapshot_dev);
        EXPECT_OK(dev.initialize(emu.config()));

        EXPECT_THAT(
                dev.getQemuParameters(emu.config()),
                ElementsAre(
                        Eq("-device"),
                        AllOf(testing::Not(HasSubstr("GuestVulkanOnly:enabled")),
                              testing::Not(HasSubstr("VulkanNativeSwapchain:enabled")),
                              testing::Not(HasSubstr("VulkanVirtualQueue:enabled")),
                              testing::Not(HasSubstr("VulkanSnapshots:enabled")),
                              testing::Not(HasSubstr("VulkanUseDedicatedAhbMemoryType:enabled")),
                              HasSubstr("x-gfxstream-gles=on"),
                              HasSubstr("renderer_features=VulkanBatchedDescriptorSetUpdate:"
                                        "enabled;"),
                              HasSubstr("BypassVulkanDeviceFeatureOverrides:enabled"),
                              HasSubstr("VulkanAllocateDeviceMemoryOnly:enabled"),
                              HasSubstr("VulkanAllocateHostMemory:enabled"),
                              HasSubstr("VulkanDebugUtils:enabled"),
                              HasSubstr("VulkanCommandBufferCheckpoints:enabled"),
                              HasSubstr("VulkanRobustness:enabled"),
                              HasSubstr("VulkanProtectedMemoryEmulation:enabled"))));
    }

    // With skiagl, VulkanVirtualQueue is disabled by default unless explicitly overridden on;
    // with graphite, VulkanVirtualQueue is enabled with default feature flags.
    {
        char skiagl[] = "skiagl";
        FakeEmulator skiagl_off_emu(AndroidOptions{.systemui_renderer = skiagl});
        EXPECT_CALL(skiagl_off_emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));
        EXPECT_CALL(skiagl_off_emu.mock_avd(), GetContentPath())
                .WillRepeatedly(Return(avd_content));

        SnapshotDevice snapshot_dev;
        EXPECT_OK(snapshot_dev.initialize(skiagl_off_emu.config()));
        GpuDevice dev("gpu0", snapshot_dev);
        EXPECT_OK(dev.initialize(skiagl_off_emu.config()));
        EXPECT_THAT(dev.getQemuParameters(skiagl_off_emu.config()),
                    ElementsAre(Eq("-device"), Not(HasSubstr("VulkanVirtualQueue:enabled"))));

        FakeEmulator skiagl_on_emu(AndroidOptions{.systemui_renderer = skiagl});
        skiagl_on_emu.feature_flags().SetEnabledOverride(S::VULKAN_VIRTUAL_QUEUE, true);
        EXPECT_CALL(skiagl_on_emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));
        EXPECT_CALL(skiagl_on_emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(avd_content));
        EXPECT_THAT(dev.getQemuParameters(skiagl_on_emu.config()),
                    ElementsAre(Eq("-device"), HasSubstr("VulkanVirtualQueue:enabled")));

        char graphite[] = "graphite";
        FakeEmulator graphite_emu(AndroidOptions{.systemui_renderer = graphite});
        EXPECT_CALL(graphite_emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));
        EXPECT_CALL(graphite_emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(avd_content));
        EXPECT_THAT(dev.getQemuParameters(graphite_emu.config()),
                    ElementsAre(Eq("-device"), HasSubstr("VulkanVirtualQueue:enabled")));
    }
}

TEST(GpuDeviceTest, Api36ConfiguresGoldfishPipeGlobalsAndSnapshotDirectory) {
    base::TestSystem sys("");
    const fs::path avd_content = sys.GetTempRoot()->Path() / "avd";
    ASSERT_OK(base::file::mkdir_recursive(avd_content, 0755));

    FakeEmulator emu;
    HardwareConfig hw;
    hw.hw_lcd_width = 1080;
    hw.hw_lcd_height = 2400;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(avd_content));
    EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(Return(36));

    SnapshotDevice snapshot_dev;
    ASSERT_OK(snapshot_dev.initialize(emu.config()));
    ASSERT_TRUE(snapshot_dev.snapshots_enabled());

    GpuDevice dev("gpu0", snapshot_dev);
    ASSERT_OK(dev.initialize(emu.config()));
    const auto env = dev.getQemuEnv(emu.config());
    for (const auto& [key, value] : env) {
        EXPECT_THAT(value, Not(HasSubstr("swiftshader"))) << key;
    }
    const auto params = dev.getQemuParameters(emu.config());
    for (const std::string& param : params) {
        EXPECT_THAT(param, Not(HasSubstr("swiftshader")));
    }
    const fs::path expected_snapshot_dir = avd_content / "snapshots" / "";

    EXPECT_THAT(params, Not(testing::Contains(Eq("-device"))));
    EXPECT_THAT(params, testing::Contains(Eq("-global")));
    EXPECT_THAT(params, testing::Contains(Eq("driver=goldfish_pipe,property=xres,value=1080")));
    EXPECT_THAT(params, testing::Contains(Eq("driver=goldfish_pipe,property=yres,value=2400")));
    EXPECT_THAT(params,
                testing::Contains(Eq("driver=goldfish_pipe,property=guest_api_level,value=36")));
    EXPECT_THAT(params,
                testing::Contains(StartsWith("driver=goldfish_pipe,property=renderer_features,"
                                             "value=")));
    EXPECT_THAT(params, testing::Contains(HasSubstr("Vulkan:enabled")));
    EXPECT_THAT(params, Not(testing::Contains(HasSubstr("VulkanSnapshots:enabled"))));
    EXPECT_THAT(params, testing::Contains(HasSubstr("GlAsyncSwap:enabled")));
    EXPECT_THAT(params, testing::Contains(HasSubstr("GlDirectMem:enabled")));
    EXPECT_THAT(params, testing::Contains(HasSubstr("HasSharedSlotsHostMemoryAllocator:enabled")));
    EXPECT_THAT(params, testing::Contains(HasSubstr("RefCountPipe:enabled")));
    EXPECT_THAT(params, testing::Contains(HasSubstr("YuvCache:enabled")));
    EXPECT_THAT(params, testing::Contains(HasSubstr("HostComposition:enabled")));
    EXPECT_THAT(params, Not(testing::Contains(HasSubstr("VulkanVirtualQueue:enabled"))));
    EXPECT_THAT(params, Not(testing::Contains(HasSubstr("GuestVulkanOnly:enabled"))));
    EXPECT_THAT(params,
                Not(testing::Contains(HasSubstr("VulkanUseDedicatedAhbMemoryType:enabled"))));
    EXPECT_THAT(params, testing::Contains(Eq(absl::StrCat(
                                "driver=goldfish_pipe,property=snapshot_directory,value=",
                                Device::EscapeQemuOptionValue(expected_snapshot_dir.string())))));
}

TEST(GpuDeviceTest, LegacyGoldfishPipeGatesVulkanBetweenApi31AndApi36) {
    base::TestSystem sys("");
    const fs::path avd_content = sys.GetTempRoot()->Path() / "avd";
    ASSERT_OK(base::file::mkdir_recursive(avd_content, 0755));

    HardwareConfig hw;
    hw.hw_lcd_width = 1080;
    hw.hw_lcd_height = 2400;

    // API 31 enables Vulkan on goldfish_pipe while keeping VulkanSnapshots disabled.
    {
        FakeEmulator emu;
        EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));
        EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(avd_content));
        EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(Return(31));

        SnapshotDevice snapshot_dev;
        ASSERT_OK(snapshot_dev.initialize(emu.config()));
        GpuDevice dev("gpu0", snapshot_dev);
        ASSERT_OK(dev.initialize(emu.config()));

        const auto params = dev.getQemuParameters(emu.config());
        EXPECT_THAT(params, testing::Contains(HasSubstr("Vulkan:enabled")));
        EXPECT_THAT(params, Not(testing::Contains(HasSubstr("VulkanSnapshots:enabled"))));
    }

    // API 30 disables Vulkan and omits VulkanSnapshots on goldfish_pipe.
    {
        FakeEmulator emu;
        EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));
        EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(avd_content));
        EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(Return(30));

        SnapshotDevice snapshot_dev;
        ASSERT_OK(snapshot_dev.initialize(emu.config()));
        GpuDevice dev("gpu0", snapshot_dev);
        ASSERT_OK(dev.initialize(emu.config()));

        const auto params = dev.getQemuParameters(emu.config());
        EXPECT_THAT(params, testing::Contains(HasSubstr("Vulkan:disabled")));
        EXPECT_THAT(params, Not(testing::Contains(HasSubstr("Vulkan:enabled"))));
        EXPECT_THAT(params, Not(testing::Contains(HasSubstr("VulkanSnapshots:enabled"))));
    }
}

TEST(GpuDeviceTest, Api36GlobalEscapesCommasInRendererFeaturesAndSnapshotDirectory) {
    base::TestSystem sys("");
    const fs::path avd_content = sys.GetTempRoot()->Path() / "avd,with,comma";
    ASSERT_OK(base::file::mkdir_recursive(avd_content, 0755));

    char renderer_features[] = "Foo:enabled,Bar:disabled";
    AndroidOptions opts{.renderer_features = renderer_features};
    FakeEmulator emu(opts);
    HardwareConfig hw;
    hw.hw_lcd_width = 720;
    hw.hw_lcd_height = 1280;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));
    EXPECT_CALL(emu.mock_avd(), GetContentPath()).WillRepeatedly(Return(avd_content));
    EXPECT_CALL(emu.mock_avd(), ApiLevel()).WillRepeatedly(Return(36));

    SnapshotDevice snapshot_dev;
    ASSERT_OK(snapshot_dev.initialize(emu.config()));
    GpuDevice dev("gpu0", snapshot_dev);
    ASSERT_OK(dev.initialize(emu.config()));

    const fs::path expected_snapshot_dir = avd_content / "snapshots" / "";
    const auto params = dev.getQemuParameters(emu.config());
    EXPECT_THAT(params, testing::Contains(HasSubstr(";Foo:enabled,,Bar:disabled")));
    EXPECT_THAT(params,
                testing::Contains(Eq("driver=goldfish_pipe,property=guest_api_level,value=36")));
    EXPECT_THAT(params, testing::Contains(Eq(absl::StrCat(
                                "driver=goldfish_pipe,property=snapshot_directory,value=",
                                Device::EscapeQemuOptionValue(expected_snapshot_dir.string())))));
}

TEST(GpuDeviceTest, CliRendererFeaturesAppendedAfterFeatureFlagEntries) {
    base::TestSystem sys("");
    char cli_renderer_features[] = "VulkanBatchedDescriptorSetUpdate:enabled";
    AndroidOptions opts{.renderer_features = cli_renderer_features};
    FakeEmulator emu(opts);
    HardwareConfig hw;
    hw.hw_lcd_width = 1080;
    hw.hw_lcd_height = 2400;
    EXPECT_CALL(emu.mock_avd(), Hw()).WillRepeatedly(ReturnRef(hw));

    SnapshotDevice snapshot_dev;
    GpuDevice dev("gpu0", snapshot_dev);
    EXPECT_OK(dev.initialize(emu.config()));

    const std::vector<std::string> params = dev.getQemuParameters(emu.config());
    ASSERT_EQ(params.size(), 2U);
    const size_t default_pos = params[1].find("VulkanBatchedDescriptorSetUpdate:disabled");
    const size_t cli_pos = params[1].find("VulkanBatchedDescriptorSetUpdate:enabled");
    ASSERT_NE(default_pos, std::string::npos);
    ASSERT_NE(cli_pos, std::string::npos);
    EXPECT_LT(default_pos, cli_pos);
}

}  // namespace android::goldfish::test
