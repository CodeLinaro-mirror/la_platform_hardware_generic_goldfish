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

#include "emulator/launcher/gpu_selection.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/log/scoped_mock_log.h"
#include "android/base/testing/test_system.h"
#include "android/base/testing/test_temp_dir.h"
#include "android/cmdline_definitions.h"
#include "android/cpu/cpu_accelerator.h"
#include "android/goldfish/constants.h"
#include "android/goldfish/hardware_config.h"
#include "android/goldfish/mock_avd.h"
#include "android/status/status_matcher_macros.h"
#include "vulkan/vulkan.h"

namespace goldfish::gpu::test {

using ::absl::ScopedMockLog;
using ::android::goldfish::FeatureFlags;
using ::android::goldfish::HardwareConfig;
using ::android::goldfish::kVirtioReadyApiLevel;
using ::android::goldfish::MockAvd;
using ::android::goldfish::SystemImagePaths;
using ::testing::_;
using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::Return;
using ::testing::ReturnRef;

namespace {

VulkanGpuDeviceInfo MakeGpuDevice(std::string_view name, uint32_t vendor_id, uint32_t device_id,
                                  VkPhysicalDeviceType device_type, uint32_t api_version,
                                  uint32_t driver_version, uint64_t memory_bytes,
                                  bool has_graphics_queue = true) {
    VulkanGpuDeviceInfo dev{};
    dev.properties.apiVersion = api_version;
    dev.properties.driverVersion = driver_version;
    dev.properties.vendorID = vendor_id;
    dev.properties.deviceID = device_id;
    dev.properties.deviceType = device_type;
    std::strncpy(dev.properties.deviceName, name.data(),
                 std::min(name.size(), sizeof(dev.properties.deviceName) - 1));
    dev.device_local_memory_bytes = memory_bytes;
    dev.has_graphics_queue_family = has_graphics_queue;
    return dev;
}

}  // namespace

class GpuSelectionTest : public ::testing::Test {
  protected:
    void SetUp() override {
        EXPECT_CALL(avd_, ApiLevel()).WillRepeatedly(Return(kVirtioReadyApiLevel));
        EXPECT_CALL(avd_, Hw()).WillRepeatedly(ReturnRef(hw_));
        EXPECT_CALL(avd_, GetSystemImagePaths()).WillRepeatedly(ReturnRef(system_image_paths_));
    }

    ::android::base::TestSystem sys_{""};
    MockAvd avd_;
    SystemImagePaths system_image_paths_;
    HardwareConfig hw_;
    FeatureFlags feature_flags_;
};

#if defined(__APPLE__)
TEST_F(GpuSelectionTest, MacOsMoltenVkSelection) {
    sys_.EnvSet("ANDROID_EMU_VK_SELECT_ICD", "moltenvk");
    char gpu_mode[] = "host";
    AndroidOptions opts{.gpu = gpu_mode};

    const GpuSelection selection = SelectGpu(avd_, opts, feature_flags_, {});
    EXPECT_EQ(selection.chosen_gpu_mode, "host");
    EXPECT_EQ(selection.emu_renderer, "host");
    EXPECT_EQ(selection.vulkan_icd, "moltenvk");
}
#endif

TEST_F(GpuSelectionTest, SelectVulkanGpuDeviceHandlesEmptyAndSingleDevices) {
    EXPECT_FALSE(SelectVulkanGpuDevice({}).has_value());

    const VulkanGpuDeviceInfo compute_only =
            MakeGpuDevice("Compute Only Accelerator", 0x10de, 0x1234,
                          VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, VK_MAKE_API_VERSION(0, 1, 3, 0),
                          535, 8589934592ULL, /*has_graphics_queue=*/false);
    EXPECT_FALSE(SelectVulkanGpuDevice(std::vector<VulkanGpuDeviceInfo>{compute_only}).has_value());

    const VulkanGpuDeviceInfo llvmpipe = MakeGpuDevice(
            "llvmpipe (LLVM 21.1.8, 256 bits)", 0x10005, 0x0, VK_PHYSICAL_DEVICE_TYPE_CPU,
            VK_MAKE_API_VERSION(0, 1, 4, 305), 1, 0ULL, /*has_graphics_queue=*/true);
    sys_.EnvSet("ANDROID_EMU_VK_SELECT_GPU", "nonexistent");
    const std::optional<VulkanGpuDeviceInfo> selected =
            SelectVulkanGpuDevice(std::vector<VulkanGpuDeviceInfo>{llvmpipe});
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->DeviceName(), "llvmpipe (LLVM 21.1.8, 256 bits)");
}

TEST_F(GpuSelectionTest, SelectsBestGpuInHostMode) {
    std::vector<VulkanGpuDeviceInfo> host_gpus = {
        MakeGpuDevice("Intel(R) UHD Graphics", 0x8086, 0x3e92,
                      VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, VK_MAKE_API_VERSION(0, 1, 2, 0), 100,
                      4294967296ULL),
        MakeGpuDevice("NVIDIA GeForce RTX 4080", 0x10de, 0x2704,
                      VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, VK_MAKE_API_VERSION(0, 1, 3, 250), 535,
                      17179869184ULL),
    };

    char gpu_mode[] = "host";
    AndroidOptions opts{.gpu = gpu_mode};
    const GpuSelection selection = SelectGpu(avd_, opts, feature_flags_, std::move(host_gpus));

    EXPECT_EQ(selection.chosen_gpu_mode, "host");
    EXPECT_EQ(selection.emu_renderer, "host");
#if defined(__APPLE__)
    EXPECT_EQ(selection.vulkan_icd, "kosmickrisp");
#else
    EXPECT_EQ(selection.vulkan_icd, "");
#endif

    ASSERT_EQ(selection.host_gpus.size(), 2U);
    EXPECT_EQ(selection.host_gpus[0].DeviceName(), "Intel(R) UHD Graphics");
    EXPECT_EQ(selection.host_gpus[0].device_local_memory_bytes, 4294967296ULL);
    EXPECT_EQ(selection.host_gpus[1].DeviceName(), "NVIDIA GeForce RTX 4080");
    EXPECT_EQ(selection.host_gpus[1].device_local_memory_bytes, 17179869184ULL);

    ASSERT_TRUE(selection.selected_gpu.has_value());
    EXPECT_EQ(selection.selected_gpu->DeviceName(), "NVIDIA GeForce RTX 4080");
    EXPECT_EQ(selection.selected_gpu->device_local_memory_bytes, 17179869184ULL);
}

TEST_F(GpuSelectionTest, SelectGpuRespectsEnvVarOverrideByIndexAndName) {
    const std::vector<VulkanGpuDeviceInfo> host_gpus = {
        MakeGpuDevice("Intel(R) UHD Graphics", 0x8086, 0x3e92,
                      VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, VK_MAKE_API_VERSION(0, 1, 2, 0), 100,
                      4294967296ULL),
        MakeGpuDevice("NVIDIA GeForce RTX 4080", 0x10de, 0x2704,
                      VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, VK_MAKE_API_VERSION(0, 1, 3, 250), 535,
                      17179869184ULL),
    };

    char gpu_mode[] = "host";
    AndroidOptions opts{.gpu = gpu_mode};

    sys_.EnvSet("ANDROID_EMU_VK_SELECT_GPU", "0");
    {
        const GpuSelection selection = SelectGpu(avd_, opts, feature_flags_, host_gpus);
        ASSERT_TRUE(selection.selected_gpu.has_value());
        EXPECT_EQ(selection.selected_gpu->DeviceName(), "Intel(R) UHD Graphics");
    }

    sys_.EnvSet("ANDROID_EMU_VK_SELECT_GPU", "uhd graphics");
    {
        const GpuSelection selection = SelectGpu(avd_, opts, feature_flags_, host_gpus);
        ASSERT_TRUE(selection.selected_gpu.has_value());
        EXPECT_EQ(selection.selected_gpu->DeviceName(), "Intel(R) UHD Graphics");
    }
}

TEST_F(GpuSelectionTest, SelectsLavapipeWithoutSoftwareProbingInDefaultSoftwareMode) {
    std::vector<VulkanGpuDeviceInfo> host_gpus = {
        MakeGpuDevice("NVIDIA GeForce RTX 4080", 0x10de, 0x2704,
                      VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, VK_MAKE_API_VERSION(0, 1, 3, 250), 535,
                      17179869184ULL),
    };

    AndroidOptions opts{};
    const GpuSelection selection = SelectGpu(avd_, opts, feature_flags_, std::move(host_gpus));

    EXPECT_EQ(selection.chosen_gpu_mode, "auto");
    EXPECT_EQ(selection.emu_renderer, "lavapipe");
    EXPECT_EQ(selection.vulkan_icd, "lavapipe");

    ASSERT_EQ(selection.host_gpus.size(), 1U);
    EXPECT_EQ(selection.host_gpus[0].DeviceName(), "NVIDIA GeForce RTX 4080");
    EXPECT_FALSE(selection.selected_gpu.has_value());
}

TEST_F(GpuSelectionTest, ForceGpuModeFeatureFlags) {
    using S = android_studio::EmulatorFeatureFlagState;

    // ForceGpuHost overrides default auto (lavapipe) mode to host mode.
    {
        FeatureFlags flags;
        flags.SetEnabledOverride(S::FORCE_GPU_HOST, true);
        AndroidOptions opts{};
        const GpuSelection selection = SelectGpu(avd_, opts, flags, {});
        EXPECT_EQ(selection.chosen_gpu_mode, "host");
        EXPECT_EQ(selection.emu_renderer, "host");
    }

    // ForceLavapipe / ForceSwiftshader / ForceGpuSoftware / ForceAngle override -gpu host.
    for (auto flag :
         {S::FORCE_GPU_SOFTWARE, S::FORCE_LAVAPIPE, S::FORCE_SWIFTSHADER, S::FORCE_ANGLE}) {
        char gpu_mode[] = "host";
        AndroidOptions opts{.gpu = gpu_mode};
        FeatureFlags flags;
        flags.SetEnabledOverride(flag, true);

        const GpuSelection selection = SelectGpu(avd_, opts, flags, {});
        EXPECT_EQ(selection.chosen_gpu_mode, "lavapipe");
        EXPECT_EQ(selection.emu_renderer, "lavapipe");
        EXPECT_EQ(selection.vulkan_icd, "lavapipe");
        EXPECT_FALSE(selection.selected_gpu.has_value());
    }
}

// Host GPU is not available on the goldfish_pipe (API < 37) path yet: -gpu host
// falls back to lavapipe with a warning, never to SwiftShader (b/570473107).
TEST_F(GpuSelectionTest, Api36HostGpuModeFallsBackToLavapipeWithWarning) {
    EXPECT_CALL(avd_, ApiLevel()).WillRepeatedly(Return(36));

    char gpu_mode[] = "host";
    AndroidOptions opts{.gpu = gpu_mode};

    ScopedMockLog log(absl::MockLogDefault::kIgnoreUnexpected);
    EXPECT_CALL(log, Log(absl::LogSeverity::kWarning, _,
                         AllOf(HasSubstr("Host GPU rendering is not available below API 37"),
                               HasSubstr("'host'"), HasSubstr("lavapipe"))));
    log.StartCapturingLogs();
    const GpuSelection selection = SelectGpu(avd_, opts, feature_flags_, {});
    log.StopCapturingLogs();

    EXPECT_EQ(selection.chosen_gpu_mode, "host");
    EXPECT_EQ(selection.emu_renderer, "lavapipe");
    EXPECT_EQ(selection.vulkan_icd, "lavapipe");
    EXPECT_FALSE(selection.selected_gpu.has_value());
}

TEST_F(GpuSelectionTest, Api36DefaultAndForcedHostGpuModesFallBackToLavapipe) {
    using S = android_studio::EmulatorFeatureFlagState;
    EXPECT_CALL(avd_, ApiLevel()).WillRepeatedly(Return(36));

    // hw.gpu.mode=host from the AVD config.
    {
        hw_.hw_gpu_mode = "host";
        AndroidOptions opts{};
        const GpuSelection selection = SelectGpu(avd_, opts, feature_flags_, {});
        EXPECT_EQ(selection.emu_renderer, "lavapipe");
        EXPECT_EQ(selection.vulkan_icd, "lavapipe");
        hw_.hw_gpu_mode.clear();
    }

    // FORCE_GPU_HOST feature flag.
    {
        FeatureFlags flags;
        flags.SetEnabledOverride(S::FORCE_GPU_HOST, true);
        AndroidOptions opts{};
        const GpuSelection selection = SelectGpu(avd_, opts, flags, {});
        EXPECT_EQ(selection.emu_renderer, "lavapipe");
        EXPECT_EQ(selection.vulkan_icd, "lavapipe");
    }

    // Default (auto) mode without any -gpu option or flag.
    {
        AndroidOptions opts{};
        const GpuSelection selection = SelectGpu(avd_, opts, feature_flags_, {});
        EXPECT_EQ(selection.emu_renderer, "lavapipe");
        EXPECT_EQ(selection.vulkan_icd, "lavapipe");
    }
}

TEST_F(GpuSelectionTest, VendorSpecificVulkanMemoryWorkaroundsAndGpuPatterns) {
    using S = android_studio::EmulatorFeatureFlagState;

    // 1. Linux + AMD GPU in host mode enables both VULKAN_ALLOCATE_DEVICE_MEMORY_ONLY and
    // VULKAN_ALLOCATE_HOST_MEMORY when not overridden.
    {
        sys_.SetOsType(::android::base::OsType::kLinux);
        std::vector<VulkanGpuDeviceInfo> host_gpus = {
            MakeGpuDevice("AMD Radeon RX 6800", 0x1002, 0x73bf,
                          VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, VK_MAKE_API_VERSION(0, 1, 3, 0), 1,
                          17179869184ULL),
        };

        char gpu_mode[] = "host";
        AndroidOptions opts{.gpu = gpu_mode};
        FeatureFlags flags;
        EXPECT_FALSE(flags.IsEnabled(S::VULKAN_ALLOCATE_DEVICE_MEMORY_ONLY));
        EXPECT_FALSE(flags.IsEnabled(S::VULKAN_ALLOCATE_HOST_MEMORY));

        flags.ApplyGpuFeaturePatterns(ToHostGpuInfos(host_gpus));
        const GpuSelection selection = SelectGpu(avd_, opts, flags, std::move(host_gpus));
        ApplyVulkanMemoryWorkarounds(selection, flags);

        EXPECT_TRUE(flags.IsEnabled(S::VULKAN_ALLOCATE_DEVICE_MEMORY_ONLY));
        EXPECT_TRUE(flags.IsEnabled(S::VULKAN_ALLOCATE_HOST_MEMORY));
    }

    // 2. User override (-VulkanAllocateHostMemory) prevents the vendor workaround from enabling it.
    {
        sys_.SetOsType(::android::base::OsType::kLinux);
        std::vector<VulkanGpuDeviceInfo> host_gpus = {
            MakeGpuDevice("Intel(R) UHD Graphics", 0x8086, 0x3e92,
                          VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, VK_MAKE_API_VERSION(0, 1, 3, 0),
                          1, 4294967296ULL),
        };

        char gpu_mode[] = "host";
        AndroidOptions opts{.gpu = gpu_mode};
        FeatureFlags flags;
        flags.SetEnabledOverride(S::VULKAN_ALLOCATE_HOST_MEMORY, false);

        flags.ApplyGpuFeaturePatterns(ToHostGpuInfos(host_gpus));
        const GpuSelection selection = SelectGpu(avd_, opts, flags, std::move(host_gpus));
        ApplyVulkanMemoryWorkarounds(selection, flags);

        EXPECT_FALSE(flags.IsEnabled(S::VULKAN_ALLOCATE_DEVICE_MEMORY_ONLY));
        EXPECT_FALSE(flags.IsEnabled(S::VULKAN_ALLOCATE_HOST_MEMORY));
    }

    // 3. Windows + NVIDIA enables VULKAN_ALLOCATE_HOST_MEMORY under AEHD, but not under WHPX.
    {
        sys_.SetOsType(::android::base::OsType::kWindows);
        const std::vector<VulkanGpuDeviceInfo> host_gpus = {
            MakeGpuDevice("NVIDIA GeForce RTX 4080", 0x10de, 0x2704,
                          VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, VK_MAKE_API_VERSION(0, 1, 3, 0), 1,
                          17179869184ULL),
        };
        const auto prev_accel = ::android::GetCurrentCpuAccelerator();

        // AEHD: workaround applies.
        {
            ::android::SetCurrentCpuAcceleratorForTesting(::android::CPU_ACCELERATOR_AEHD,
                                                          ::android::ANDROID_CPU_ACCELERATION_READY,
                                                          "AEHD");
            char gpu_mode[] = "host";
            AndroidOptions opts{.gpu = gpu_mode};
            FeatureFlags flags;

            flags.ApplyGpuFeaturePatterns(ToHostGpuInfos(host_gpus));
            const GpuSelection selection = SelectGpu(avd_, opts, flags, host_gpus);
            ApplyVulkanMemoryWorkarounds(selection, flags);

            EXPECT_TRUE(flags.IsEnabled(S::VULKAN_ALLOCATE_HOST_MEMORY));
        }

        // WHPX: unaffected by b/382621412, so workaround must not apply.
        {
            ::android::SetCurrentCpuAcceleratorForTesting(::android::CPU_ACCELERATOR_WHPX,
                                                          ::android::ANDROID_CPU_ACCELERATION_READY,
                                                          "WHPX");
            char gpu_mode[] = "host";
            AndroidOptions opts{.gpu = gpu_mode};
            FeatureFlags flags;

            flags.ApplyGpuFeaturePatterns(ToHostGpuInfos(host_gpus));
            const GpuSelection selection = SelectGpu(avd_, opts, flags, host_gpus);
            ApplyVulkanMemoryWorkarounds(selection, flags);

            EXPECT_FALSE(flags.IsEnabled(S::VULKAN_ALLOCATE_HOST_MEMORY));
        }

        ::android::ResetCurrentCpuAccelerator(prev_accel);
    }

    // 4. GPU-dependent server pattern enabling ForceSwiftshader forces software rendering, and
    // host-GPU memory workarounds are not applied when software rendering is selected.
    {
        sys_.SetOsType(::android::base::OsType::kLinux);
        ::android::base::TestTempDir temp_dir("gpu_selection_patterns");
        const std::filesystem::path patterns_file = temp_dir.Path() / "gpu_patterns.protobuf";
        {
            std::ofstream out(patterns_file, std::ios::trunc);
            out << R"pb(
                pattern {
                  hwconfig { hostgpuinfo { make: "0x8086" device_id: "0x0412" } }
                  featureaction { feature: ForceSwiftshader enable: true }
                })pb";
        }
        std::vector<VulkanGpuDeviceInfo> host_gpus = {
            MakeGpuDevice("Intel HD Graphics 4600", 0x8086, 0x0412,
                          VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, VK_MAKE_API_VERSION(0, 1, 2, 0),
                          1, 2147483648ULL),
        };
        ASSERT_OK_AND_ASSIGN(FeatureFlags flags,
                             FeatureFlags::Create({.default_patterns_path = patterns_file}));
        EXPECT_FALSE(flags.IsEnabled(S::FORCE_SWIFTSHADER));

        char gpu_mode[] = "host";
        AndroidOptions opts{.gpu = gpu_mode};

        flags.ApplyGpuFeaturePatterns(ToHostGpuInfos(host_gpus));
        const GpuSelection selection = SelectGpu(avd_, opts, flags, std::move(host_gpus));
        ApplyVulkanMemoryWorkarounds(selection, flags);

        EXPECT_TRUE(flags.IsEnabled(S::FORCE_SWIFTSHADER));
        EXPECT_EQ(selection.chosen_gpu_mode, "lavapipe");
        EXPECT_EQ(selection.emu_renderer, "lavapipe");
        EXPECT_EQ(selection.vulkan_icd, "lavapipe");
        EXPECT_FALSE(flags.IsEnabled(S::VULKAN_ALLOCATE_HOST_MEMORY));
    }
}

}  // namespace goldfish::gpu::test
