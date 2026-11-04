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

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "goldfish/gvk/i_meta_loader.h"
#include "vulkan/vulkan.h"

namespace goldfish::gpu {

// Safely constructs a string_view from a fixed-size C buffer that may or may
// not be NUL-terminated (e.g. Vulkan char[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE]).
template <size_t N>
constexpr std::string_view BoundedCStrView(const char (&buf)[N]) noexcept {
    const char* nul = std::char_traits<char>::find(buf, N, '\0');
    return std::string_view(buf, nul != nullptr ? static_cast<size_t>(nul - buf) : N);
}

struct VulkanGpuDeviceInfo {
    VkPhysicalDeviceProperties properties{};
    uint64_t device_local_memory_bytes = 0;
    bool has_graphics_queue_family = false;

    constexpr std::string_view DeviceName() const noexcept {
        return BoundedCStrView(properties.deviceName);
    }
};

// Returns the Vulkan ICD name used for physical host GPU probing and execution
// ("" for the system default on Linux/Windows; "kosmickrisp" or "moltenvk" on macOS).
std::string_view GetHostVulkanIcd();

// Probes Vulkan physical devices for `vk_icd` using the loader in `vulkan_libdir`.
std::vector<VulkanGpuDeviceInfo> ProbeVulkanGpuDevices(const std::filesystem::path& vulkan_libdir,
                                                       std::string_view vk_icd);

// Overrides the Vulkan meta-loader used by ProbeVulkanGpuDevices(). Pass nullptr
// to restore the system loader.
void SetVulkanLoaderForTesting(::goldfish::gvk::IMetaLoader::Ptr loader);

}  // namespace goldfish::gpu
