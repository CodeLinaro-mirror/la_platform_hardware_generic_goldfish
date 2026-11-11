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

#include "emulator/launcher/gpu_info.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"

#include "android/base/system.h"
#include "goldfish/file/file.h"
#include "goldfish/gvk/instance_dispatch.h"
#include "goldfish/gvk/system_meta_loader.h"
#include "vulkan/vulkan.h"

namespace goldfish::gpu {
namespace {

using ::android::base::System;

::goldfish::gvk::IMetaLoader::Ptr& VulkanLoaderOverride() {
    static absl::NoDestructor<::goldfish::gvk::IMetaLoader::Ptr> override_loader;
    return *override_loader;
}

class ScopedVulkanEnvGuard {
  public:
    ScopedVulkanEnvGuard() {
        for (size_t i = 0; i < kEnvVars.size(); ++i) {
            saved_os_values_[i] = System::GetEnvironmentVariable(kEnvVars[i]);
        }
    }

    ~ScopedVulkanEnvGuard() {
        for (size_t i = 0; i < kEnvVars.size(); ++i) {
            System::SetEnvironmentVariable(kEnvVars[i], saved_os_values_[i]);
        }
    }

    ScopedVulkanEnvGuard(const ScopedVulkanEnvGuard&) = delete;
    ScopedVulkanEnvGuard& operator=(const ScopedVulkanEnvGuard&) = delete;
    ScopedVulkanEnvGuard(ScopedVulkanEnvGuard&&) = delete;
    ScopedVulkanEnvGuard& operator=(ScopedVulkanEnvGuard&&) = delete;

  private:
    static constexpr std::array<std::string_view, 4> kEnvVars = {
        "VK_DRIVER_FILES",
        "VK_ICD_FILENAMES",
        "VK_ADD_DRIVER_FILES",
        "VK_LOADER_DRIVERS_DISABLE",
    };
    std::array<std::string, 4> saved_os_values_;
};

std::string_view GetIcdJsonFilename(std::string_view vk_icd) {
    if (vk_icd == "kosmickrisp") {
        return "libkosmickrisp_icd.json";
    }
    if (vk_icd == "moltenvk") {
        return "MoltenVK_icd.json";
    }
    if (vk_icd == "lavapipe") {
        return "lvp_icd.json";
    }
    if (vk_icd == "swiftshader") {
        return "vk_swiftshader_icd.json";
    }
    return "";
}

}  // namespace

std::string_view GetHostVulkanIcd() {
#if defined(__APPLE__)
    // Use KosmicKrisp as the default Vulkan ICD on macOS, allow changing to MoltenVK with an
    // environment variable.
    return (System::Get()->EnvGet("ANDROID_EMU_VK_SELECT_ICD") == "moltenvk") ? "moltenvk"
                                                                              : "kosmickrisp";
#else
    // Use system defaults.
    return "";
#endif
}

std::vector<VulkanGpuDeviceInfo> ProbeVulkanGpuDevices(const std::filesystem::path& vulkan_libdir,
                                                       std::string_view vk_icd) {
    if (vulkan_libdir.empty() || !android::base::file::is_dir(vulkan_libdir)) {
        return {};
    }

    ScopedVulkanEnvGuard env_guard;

    System::SetEnvironmentVariable("VK_ICD_FILENAMES", "");
    System::SetEnvironmentVariable("VK_ADD_DRIVER_FILES", "");

    if (vk_icd.empty()) {
        System::SetEnvironmentVariable("VK_DRIVER_FILES", "");
        // Disable CPU software rasterizers (Lavapipe and SwiftShader) when
        // probing for physical host GPUs:
        // 1. We only want physical/virtual GPU devices in host_gpus, not CPU
        //    software fallback drivers.
        // 2. Loading Lavapipe (e.g. via a system /usr/share/vulkan/icd.d/lvp_icd.json
        //    manifest on Linux) pulls in and initializes libLLVM.so (~75 MB)
        //    during extension enumeration and instance creation, adding seconds
        //    of startup latency and risking netsimd startup inactivity timeouts
        //    on slow or cold-cache hosts.
        System::SetEnvironmentVariable("VK_LOADER_DRIVERS_DISABLE", "*lvp*,*swiftshader*");
    } else {
        System::SetEnvironmentVariable("VK_LOADER_DRIVERS_DISABLE", "");
        const std::string_view icd_filename = GetIcdJsonFilename(vk_icd);
        if (icd_filename.empty()) {
            LOG(WARNING) << "Unknown Vulkan ICD for GPU probe: " << vk_icd;
            return {};
        }
        const std::filesystem::path icd_path = vulkan_libdir / icd_filename;
        if (!android::base::file::is_file(icd_path)) {
            LOG(WARNING) << "Vulkan ICD file not found: " << icd_path.string();
            return {};
        }
        const auto abs_icd_path = android::base::file::make_absolute(icd_path);
        if (!abs_icd_path.ok()) {
            LOG(WARNING) << "Failed to resolve Vulkan ICD path: " << abs_icd_path.status();
            return {};
        }
        System::SetEnvironmentVariable("VK_DRIVER_FILES", abs_icd_path->string());
    }

    const auto& override_loader = VulkanLoaderOverride();
    const auto loader =
            override_loader
                    ? override_loader
                    : ::goldfish::gvk::SystemMetaLoader::get(vulkan_libdir.string().c_str());
    if (!loader) {
        LOG(WARNING) << "Failed to get Vulkan system meta loader from " << vulkan_libdir.string();
        return {};
    }

    const uint32_t instance_version = loader->enumerateInstanceVersion();
    if (instance_version < VK_MAKE_API_VERSION(0, 1, 1, 0)) {
        LOG(WARNING) << absl::StrFormat(
                "Vulkan instance version too low: %d.%d.%d (expected >= 1.1.0)",
                VK_API_VERSION_MAJOR(instance_version), VK_API_VERSION_MINOR(instance_version),
                VK_API_VERSION_PATCH(instance_version));
        return {};
    }

    const VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "gpu_device_probe",
        .applicationVersion = VK_MAKE_API_VERSION(0, 1, 1, 0),
        .pEngineName = "android_emulator",
        .engineVersion = VK_MAKE_API_VERSION(0, 1, 1, 0),
        .apiVersion = VK_MAKE_API_VERSION(0, 1, 1, 0),
    };

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = 0,
        .ppEnabledExtensionNames = nullptr,
    };

#if defined(__APPLE__)
    // Portability drivers on macOS (such as MoltenVK) require
    // VK_KHR_portability_enumeration to be enumerated. Skip querying instance
    // extension properties on other platforms because each call to
    // vkEnumerateInstanceExtensionProperties(nullptr, ...) when no VkInstance
    // exists causes the Vulkan loader to scan, dlopen, and dlclose every
    // system ICD before vkCreateInstance loads them again.
    // Note: VK_KHR_portability_enumeration is built into the Khronos Vulkan
    // Loader starting from Vulkan 1.3.216, so in the future
    // enumerateInstanceExtensionProperties(nullptr) can also be skipped on
    // macOS when the Vulkan loader version is guaranteed to be >= 1.3.216.
    std::vector<const char*> enabled_extensions;
    const std::vector<VkExtensionProperties> instance_exts =
            loader->enumerateInstanceExtensionProperties(nullptr);
    for (const auto& ext : instance_exts) {
        if (BoundedCStrView(ext.extensionName) == VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) {
            create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            enabled_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            break;
        }
    }
    create_info.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size());
    create_info.ppEnabledExtensionNames = enabled_extensions.data();
#endif

    const auto instance_dispatch = ::goldfish::gvk::InstanceDispatch::create(loader, create_info);
    if (!instance_dispatch) {
        return {};
    }

    uint32_t dev_count = 0;
    VkResult result = instance_dispatch->enumeratePhysicalDevices(&dev_count, nullptr);
    if (result != VK_SUCCESS) {
        LOG(WARNING) << "`enumeratePhysicalDevices` failed with " << result;
        return {};
    }
    if (dev_count == 0) {
        return {};
    }

    std::vector<VkPhysicalDevice> devs(dev_count);
    result = instance_dispatch->enumeratePhysicalDevices(&dev_count, devs.data());
    if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
        LOG(WARNING) << "`enumeratePhysicalDevices` failed with " << result;
        return {};
    }
    devs.resize(dev_count);

    std::vector<VulkanGpuDeviceInfo> probed_devices;
    probed_devices.reserve(dev_count);
    for (const VkPhysicalDevice dev : devs) {
        VulkanGpuDeviceInfo probed;
        probed.properties = instance_dispatch->getPhysicalDeviceProperties(dev);

        const VkPhysicalDeviceMemoryProperties mem_props =
                instance_dispatch->getPhysicalDeviceMemoryProperties(dev);
        for (uint32_t i = 0; i < mem_props.memoryHeapCount; ++i) {
            if ((mem_props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
                probed.device_local_memory_bytes += mem_props.memoryHeaps[i].size;
            }
        }

        const std::vector<VkQueueFamilyProperties> queue_props =
                instance_dispatch->getPhysicalDeviceQueueFamilyProperties(dev);
        probed.has_graphics_queue_family =
                std::ranges::any_of(queue_props, [](const VkQueueFamilyProperties& queue_family) {
                    return queue_family.queueCount > 0 &&
                           (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
                });

        probed_devices.push_back(probed);
    }
    return probed_devices;
}

void SetVulkanLoaderForTesting(::goldfish::gvk::IMetaLoader::Ptr loader) {
    VulkanLoaderOverride() = std::move(loader);
}

}  // namespace goldfish::gpu
