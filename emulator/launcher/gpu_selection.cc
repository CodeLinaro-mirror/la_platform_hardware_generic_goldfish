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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"

#include "android/base/system.h"
#include "android/cpu/cpu_accelerator.h"
#include "android/goldfish/constants.h"
#include "android/goldfish/hardware_config.h"
#include "vulkan/vulkan.h"

namespace goldfish::gpu {
namespace {

using ::android::base::System;

constexpr uint32_t kVendorNvidia = 0x10deU;
constexpr uint32_t kVendorIntel = 0x8086U;
constexpr uint32_t kVendorAmd = 0x1002U;

enum class GpuVendor : uint8_t {
    kUnknown,
    kNvidia,
    kIntel,
    kAmd,
};

GpuVendor DetectGpuVendor(const VkPhysicalDeviceProperties& props) {
    const std::string_view device_name = BoundedCStrView(props.deviceName);
    if (props.vendorID == kVendorNvidia || absl::StrContainsIgnoreCase(device_name, "NVIDIA")) {
        return GpuVendor::kNvidia;
    }
    if (props.vendorID == kVendorIntel || absl::StrContainsIgnoreCase(device_name, "Intel")) {
        return GpuVendor::kIntel;
    }
    if (props.vendorID == kVendorAmd || absl::StartsWithIgnoreCase(device_name, "AMD") ||
        absl::StrContainsIgnoreCase(device_name, "Radeon")) {
        return GpuVendor::kAmd;
    }
    return GpuVendor::kUnknown;
}

// Keep the GPU selection algorithm in sync with VkEmulation::getSelectedGpuIndex
// in gfxstream (vk_common_operations.cpp) so the reported active GPU matches
// the device selected by gfxstream at runtime.
std::optional<size_t> SelectVulkanGpuDeviceIndex(std::span<const VulkanGpuDeviceInfo> devices) {
    if (devices.empty()) {
        return std::nullopt;
    }

    // ANDROID_EMU_VK_SELECT_GPU is ignored when there is only one physical device,
    // matching VkEmulation::getSelectedGpuIndex in vk_common_operations.cpp.
    if (devices.size() == 1) {
        return devices[0].has_graphics_queue_family ? std::optional<size_t>(0) : std::nullopt;
    }

    const std::string enforced_gpu = System::Get()->EnvGet("ANDROID_EMU_VK_SELECT_GPU");
    if (!enforced_gpu.empty()) {
        std::optional<size_t> enforced_index;
        if (size_t parsed_index = 0; absl::SimpleAtoi(enforced_gpu, &parsed_index)) {
            if (parsed_index < devices.size()) {
                enforced_index = parsed_index;
            }
        } else {
            const std::string lowered_target = absl::AsciiStrToLower(enforced_gpu);
            for (size_t i = 0; i < devices.size(); ++i) {
                const std::string_view raw_name = devices[i].DeviceName();
                if (absl::StrContains(absl::AsciiStrToLower(raw_name), lowered_target)) {
                    enforced_index = i;
                }
            }
        }

        if (enforced_index.has_value() && devices[*enforced_index].has_graphics_queue_family) {
            return enforced_index;
        }
        LOG(WARNING) << "Could not select GPU with ANDROID_EMU_VK_SELECT_GPU=" << enforced_gpu;
    }

    auto score_device = [](const VulkanGpuDeviceInfo& device) -> uint32_t {
        if (!device.has_graphics_queue_family) {
            return 0;
        }

        constexpr std::array<uint32_t, 5> kDeviceTypeScoreTable = {
            100,   // VK_PHYSICAL_DEVICE_TYPE_OTHER = 0
            1000,  // VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU = 1
            2000,  // VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU = 2
            500,   // VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU = 3
            600,   // VK_PHYSICAL_DEVICE_TYPE_CPU = 4
        };

        uint32_t score = 0;
        const uint32_t device_type = static_cast<uint32_t>(device.properties.deviceType);
        if (device_type < kDeviceTypeScoreTable.size()) {
            score += kDeviceTypeScoreTable[device_type];
        }

        const uint32_t major = VK_API_VERSION_MAJOR(device.properties.apiVersion);
        const uint32_t minor = VK_API_VERSION_MINOR(device.properties.apiVersion);
        const uint32_t patch = VK_API_VERSION_PATCH(device.properties.apiVersion);
        score += major * 5000U + std::min(minor, 10U) * 500U + std::min(patch, 400U);
        return score;
    };

    uint32_t max_score = 0;
    std::optional<size_t> selected_index;
    for (size_t i = 0; i < devices.size(); ++i) {
        const uint32_t score = score_device(devices[i]);
        if (score > max_score) {
            max_score = score;
            selected_index = i;
        }
    }
    return selected_index;
}

bool IsHostGpuDenyListed(std::span<const VulkanGpuDeviceInfo> host_gpus,
                         const std::optional<VulkanGpuDeviceInfo>& selected_host_gpu) {
    // TODO(b/520445586) Check server side gpu list and driver capabilities using |host_gpus| and
    // |selected_host_gpu| to determine if host is denylisted for GPU. Using software rendering by
    // default for now.
    (void)host_gpus;
    (void)selected_host_gpu;
    return true;
}

}  // namespace

::android::goldfish::HostGpuInfo ToHostGpuInfo(const VulkanGpuDeviceInfo& gpu) {
    const auto& props = gpu.properties;
    const std::string_view device_name = gpu.DeviceName();
    return ::android::goldfish::HostGpuInfo{
        .make = absl::StrFormat("0x%04x", props.vendorID),
        .model = std::string(device_name),
        .device_id = absl::StrFormat("0x%04x", props.deviceID),
        .revision_id = absl::StrFormat("0x%x", props.driverVersion),
        .version = absl::StrFormat("%d.%d.%d", VK_API_VERSION_MAJOR(props.apiVersion),
                                   VK_API_VERSION_MINOR(props.apiVersion),
                                   VK_API_VERSION_PATCH(props.apiVersion)),
        .renderer = std::string(device_name),
    };
}

std::vector<::android::goldfish::HostGpuInfo> ToHostGpuInfos(
        std::span<const VulkanGpuDeviceInfo> gpus) {
    std::vector<::android::goldfish::HostGpuInfo> infos;
    infos.reserve(gpus.size());
    for (const auto& gpu : gpus) {
        infos.push_back(ToHostGpuInfo(gpu));
    }
    return infos;
}

std::optional<VulkanGpuDeviceInfo> SelectVulkanGpuDevice(
        std::span<const VulkanGpuDeviceInfo> devices) {
    const std::optional<size_t> index = SelectVulkanGpuDeviceIndex(devices);
    if (!index.has_value()) {
        return std::nullopt;
    }
    return devices[*index];
}

GpuSelection SelectGpu(const ::android::goldfish::Avd& avd, const AndroidOptions& opts,
                       const ::android::goldfish::FeatureFlags& feature_flags,
                       std::vector<VulkanGpuDeviceInfo> host_gpus) {
    std::optional<VulkanGpuDeviceInfo> selected_host_gpu = SelectVulkanGpuDevice(host_gpus);

    // Check command line and AVD options, use auto by default.
    const std::string user_gpu_mode = opts.gpu ? std::string(opts.gpu) : "";
    const std::string avd_gpu_mode = avd.Hw().hw_gpu_mode;
    std::string chosen_gpu_mode = !user_gpu_mode.empty()
                                          ? user_gpu_mode
                                          : (!avd_gpu_mode.empty() ? avd_gpu_mode : "auto");

    using S = android_studio::EmulatorFeatureFlagState;
    if (feature_flags.IsEnabled(S::FORCE_GPU_HOST)) {
        chosen_gpu_mode = "host";
    } else if (feature_flags.IsEnabled(S::FORCE_GPU_SOFTWARE) ||
               feature_flags.IsEnabled(S::FORCE_LAVAPIPE) ||
               feature_flags.IsEnabled(S::FORCE_SWIFTSHADER) ||
               feature_flags.IsEnabled(S::FORCE_ANGLE)) {
        chosen_gpu_mode = "lavapipe";
    }

    bool use_host_gpu =
            (chosen_gpu_mode == "host") ||
            (chosen_gpu_mode == "auto" && !IsHostGpuDenyListed(host_gpus, selected_host_gpu));
    if (use_host_gpu && avd.ApiLevel() < ::android::goldfish::kVirtioReadyApiLevel) {
        // The goldfish_pipe (API < 37) path cannot drive the host GPU yet; every
        // software mode on this emulator resolves to lavapipe (SwiftShader is
        // being removed, b/570473107), so fall back to it instead of silently
        // picking a different renderer than the one that was asked for.
        LOG(WARNING) << "Host GPU rendering is not available below API "
                     << ::android::goldfish::kVirtioReadyApiLevel << " yet (this AVD is API "
                     << avd.ApiLevel() << "); requested GPU mode '" << chosen_gpu_mode
                     << "' falls back to lavapipe.";
        use_host_gpu = false;
    }

    std::string chosen_emu_renderer;
    std::string chosen_vulkan_icd;
    std::optional<VulkanGpuDeviceInfo> selected_gpu;
    if (use_host_gpu) {
        chosen_emu_renderer = "host";
        chosen_vulkan_icd = std::string(GetHostVulkanIcd());
        selected_gpu = std::move(selected_host_gpu);
    } else {
        // Use lavapipe for software or invalid modes.
        chosen_emu_renderer = "lavapipe";
        chosen_vulkan_icd = "lavapipe";
    }

    return {
        .chosen_gpu_mode = std::move(chosen_gpu_mode),
        .emu_renderer = std::move(chosen_emu_renderer),
        .vulkan_icd = std::move(chosen_vulkan_icd),
        .host_gpus = std::move(host_gpus),
        .selected_gpu = std::move(selected_gpu),
    };
}

void ApplyVulkanMemoryWorkarounds(const GpuSelection& selection,
                                  ::android::goldfish::FeatureFlags& feature_flags) {
    if (selection.emu_renderer != "host" || !selection.selected_gpu.has_value()) {
        return;
    }

    using S = android_studio::EmulatorFeatureFlagState;
    const GpuVendor vendor = DetectGpuVendor(selection.selected_gpu->properties);

    const auto os_type = System::Get()->GetOsType();
    if (os_type == ::android::base::OsType::kLinux) {
        if (vendor == GpuVendor::kAmd) {
            feature_flags.SetIfNotOverridden(S::VULKAN_ALLOCATE_DEVICE_MEMORY_ONLY, true);
        }
        // b/324086743: Turn on Host memory allocation when using Linux with Intel/AMD.
        if (vendor == GpuVendor::kIntel || vendor == GpuVendor::kAmd) {
            feature_flags.SetIfNotOverridden(S::VULKAN_ALLOCATE_HOST_MEMORY, true);
        }
    } else if (os_type == ::android::base::OsType::kWindows) {
        if (vendor == GpuVendor::kAmd) {
            feature_flags.SetIfNotOverridden(S::VULKAN_ALLOCATE_DEVICE_MEMORY_ONLY, true);
        }
        // b/382621412: Turn on Host memory allocation when using Windows with NVIDIA and AEHD.
        if (vendor == GpuVendor::kNvidia &&
            ::android::GetCurrentCpuAccelerator() == ::android::CPU_ACCELERATOR_AEHD) {
            feature_flags.SetIfNotOverridden(S::VULKAN_ALLOCATE_HOST_MEMORY, true);
        }
    }
}

}  // namespace goldfish::gpu
