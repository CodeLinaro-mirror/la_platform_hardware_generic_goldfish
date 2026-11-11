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

#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "android/cmdline_option.h"
#include "android/goldfish/avd.h"
#include "android/goldfish/feature_flags.h"
#include "emulator/launcher/gpu_info.h"

namespace goldfish::gpu {

struct GpuSelection {
    // Effective GPU mode after CLI, AVD config, and ForceGpu* feature flags
    // ("auto", "host", "lavapipe", etc.).
    std::string chosen_gpu_mode;
    // ANDROID_EMU_RENDERER value passed to QEMU ("host" or "lavapipe").
    std::string emu_renderer;
    // ANDROID_EMU_VK_ICD value passed to QEMU ("" for system default, "lavapipe",
    // "kosmickrisp", or "moltenvk").
    std::string vulkan_icd;
    // Physical/virtual GPU devices probed from the host Vulkan ICD.
    std::vector<VulkanGpuDeviceInfo> host_gpus;
    // The Vulkan device that QEMU will execute on (populated by SelectGpu in host
    // mode, or by probing the chosen software ICD in software mode).
    std::optional<VulkanGpuDeviceInfo> selected_gpu;
};

// Converts probed Vulkan device properties into the HostGpuInfo representation
// matched by server feature flag patterns.
::android::goldfish::HostGpuInfo ToHostGpuInfo(const VulkanGpuDeviceInfo& gpu);
std::vector<::android::goldfish::HostGpuInfo> ToHostGpuInfos(
        std::span<const VulkanGpuDeviceInfo> gpus);

// Selects the preferred graphics-capable Vulkan device from `devices`, honoring
// `ANDROID_EMU_VK_SELECT_GPU` when multiple devices are present.
std::optional<VulkanGpuDeviceInfo> SelectVulkanGpuDevice(
        std::span<const VulkanGpuDeviceInfo> devices);

// Selects the emulator GPU mode and Vulkan ICD from already-probed `host_gpus`.
GpuSelection SelectGpu(const ::android::goldfish::Avd& avd, const AndroidOptions& opts,
                       const ::android::goldfish::FeatureFlags& feature_flags,
                       std::vector<VulkanGpuDeviceInfo> host_gpus);

// Applies vendor-specific Vulkan memory feature flag workarounds for the
// selected physical host GPU when running in host rendering mode.
void ApplyVulkanMemoryWorkarounds(const GpuSelection& selection,
                                  ::android::goldfish::FeatureFlags& feature_flags);

}  // namespace goldfish::gpu
