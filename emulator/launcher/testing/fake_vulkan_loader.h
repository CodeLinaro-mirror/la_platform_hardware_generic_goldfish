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

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"

#include "android/base/system.h"
#include "goldfish/file/file.h"
#include "goldfish/gvk/i_meta_loader.h"
#include "vulkan/vulkan.h"

namespace goldfish::gpu::test {

struct FakePhysicalDevice {
    VkPhysicalDeviceProperties properties{};
    uint64_t device_local_memory_bytes = 0;
    bool has_graphics_queue = true;
};

inline FakePhysicalDevice MakeFakePhysicalDevice(std::string_view name, uint32_t vendor_id,
                                                 uint32_t device_id,
                                                 VkPhysicalDeviceType device_type,
                                                 uint32_t api_version, uint32_t driver_version,
                                                 uint64_t memory_bytes, bool has_graphics_queue) {
    FakePhysicalDevice dev;
    dev.properties.apiVersion = api_version;
    dev.properties.driverVersion = driver_version;
    dev.properties.vendorID = vendor_id;
    dev.properties.deviceID = device_id;
    dev.properties.deviceType = device_type;
    std::strncpy(dev.properties.deviceName, name.data(),
                 std::min(name.size(), sizeof(dev.properties.deviceName) - 1));
    dev.device_local_memory_bytes = memory_bytes;
    dev.has_graphics_queue = has_graphics_queue;
    return dev;
}

class FakeVulkanLoader : public ::goldfish::gvk::IMetaLoader {
  public:
    explicit FakeVulkanLoader(std::vector<FakePhysicalDevice> host_devices,
                              std::vector<FakePhysicalDevice> software_devices = {})
            : host_devices_(std::move(host_devices))
            , software_devices_(std::move(software_devices)) {}

    uint32_t enumerateInstanceVersion() const override { return VK_MAKE_API_VERSION(0, 1, 3, 0); }

    std::vector<VkLayerProperties> enumerateInstanceLayerProperties() const override { return {}; }

    std::vector<VkExtensionProperties> enumerateInstanceExtensionProperties(
            const char* /*layer_name*/) const override {
        return {};
    }

    size_t instances_created() const { return instances_created_.load(); }
    bool instance_destroyed() const {
        const size_t created = instances_created_.load();
        return created > 0 && created == instances_destroyed_.load();
    }
    std::string observed_vk_driver_files() const ABSL_LOCKS_EXCLUDED(obs_mu_) {
        const absl::MutexLock lock(obs_mu_);
        return observed_vk_driver_files_;
    }
    std::string observed_vk_icd_filenames() const ABSL_LOCKS_EXCLUDED(obs_mu_) {
        const absl::MutexLock lock(obs_mu_);
        return observed_vk_icd_filenames_;
    }
    std::string observed_vk_add_driver_files() const ABSL_LOCKS_EXCLUDED(obs_mu_) {
        const absl::MutexLock lock(obs_mu_);
        return observed_vk_add_driver_files_;
    }
    std::string observed_vk_loader_drivers_disable() const ABSL_LOCKS_EXCLUDED(obs_mu_) {
        const absl::MutexLock lock(obs_mu_);
        return observed_vk_loader_drivers_disable_;
    }

  private:
    VkInstance createInstance(const VkInstanceCreateInfo& /*create_info*/) const override
            ABSL_LOCKS_EXCLUDED(obs_mu_) {
        {
            const absl::MutexLock lock(obs_mu_);
            observed_vk_driver_files_ =
                    ::android::base::System::GetEnvironmentVariable("VK_DRIVER_FILES");
            observed_vk_icd_filenames_ =
                    ::android::base::System::GetEnvironmentVariable("VK_ICD_FILENAMES");
            observed_vk_add_driver_files_ =
                    ::android::base::System::GetEnvironmentVariable("VK_ADD_DRIVER_FILES");
            observed_vk_loader_drivers_disable_ =
                    ::android::base::System::GetEnvironmentVariable("VK_LOADER_DRIVERS_DISABLE");
        }
        instances_created_.fetch_add(1);
        return reinterpret_cast<VkInstance>(const_cast<FakeVulkanLoader*>(this));
    }

    PFN_vkGetInstanceProcAddr getInstanceProcAddr() const override { return &GetInstanceProcAddr; }

    const std::vector<FakePhysicalDevice>* GetActiveDevices() const ABSL_LOCKS_EXCLUDED(obs_mu_) {
        const absl::MutexLock lock(obs_mu_);
        if (!observed_vk_icd_filenames_.empty() || !observed_vk_add_driver_files_.empty()) {
            return nullptr;
        }
        if (!observed_vk_driver_files_.empty()) {
            if (!observed_vk_loader_drivers_disable_.empty()) {
                return nullptr;
            }
            const std::filesystem::path icd_path(observed_vk_driver_files_);
            if (!android::base::file::is_file(icd_path)) {
                return nullptr;
            }
            const std::string filename = icd_path.filename().string();
            if (filename == "lvp_icd.json" || filename == "vk_swiftshader_icd.json") {
                return &software_devices_;
            }
#if defined(__APPLE__)
            if (filename == "libkosmickrisp_icd.json" || filename == "MoltenVK_icd.json") {
                return &host_devices_;
            }
#endif
            return nullptr;
        }
#if !defined(__APPLE__)
        if (observed_vk_loader_drivers_disable_ == "*lvp*,*swiftshader*") {
            return &host_devices_;
        }
#endif
        return nullptr;
    }

    static VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance,
                                                      const VkAllocationCallbacks* /*allocator*/) {
        reinterpret_cast<FakeVulkanLoader*>(instance)->instances_destroyed_.fetch_add(1);
    }

    static VKAPI_ATTR VkResult VKAPI_CALL
    EnumeratePhysicalDevices(VkInstance instance, uint32_t* p_physical_device_count,
                             VkPhysicalDevice* p_physical_devices) {
        auto* self = reinterpret_cast<FakeVulkanLoader*>(instance);
        const auto* active_devices = self->GetActiveDevices();
        const size_t available_count = active_devices ? active_devices->size() : 0;
        if (!p_physical_device_count) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        if (!p_physical_devices) {
            *p_physical_device_count = static_cast<uint32_t>(available_count);
            return VK_SUCCESS;
        }
        const uint32_t count =
                std::min(*p_physical_device_count, static_cast<uint32_t>(available_count));
        for (uint32_t i = 0; i < count; ++i) {
            p_physical_devices[i] = reinterpret_cast<VkPhysicalDevice>(
                    const_cast<FakePhysicalDevice*>(&(*active_devices)[i]));
        }
        *p_physical_device_count = count;
        return count < available_count ? VK_INCOMPLETE : VK_SUCCESS;
    }

    static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceProperties(
            VkPhysicalDevice physical_device, VkPhysicalDeviceProperties* p_properties) {
        *p_properties = reinterpret_cast<const FakePhysicalDevice*>(physical_device)->properties;
    }

    static VKAPI_ATTR void VKAPI_CALL
    GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physical_device,
                                      VkPhysicalDeviceMemoryProperties* p_memory_properties) {
        const auto* dev = reinterpret_cast<const FakePhysicalDevice*>(physical_device);
        *p_memory_properties = {};
        if (dev->device_local_memory_bytes > 0) {
            p_memory_properties->memoryHeapCount = 1;
            p_memory_properties->memoryHeaps[0].size = dev->device_local_memory_bytes;
            p_memory_properties->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
        }
    }

    static VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceQueueFamilyProperties(
            VkPhysicalDevice physical_device, uint32_t* p_queue_family_property_count,
            VkQueueFamilyProperties* p_queue_family_properties) {
        const auto* dev = reinterpret_cast<const FakePhysicalDevice*>(physical_device);
        if (!p_queue_family_property_count) {
            return;
        }
        if (!p_queue_family_properties) {
            *p_queue_family_property_count = 1;
            return;
        }
        if (*p_queue_family_property_count > 0) {
            p_queue_family_properties[0] = {};
            p_queue_family_properties[0].queueCount = 1;
            p_queue_family_properties[0].queueFlags =
                    dev->has_graphics_queue ? VK_QUEUE_GRAPHICS_BIT : VK_QUEUE_COMPUTE_BIT;
            *p_queue_family_property_count = 1;
        }
    }

    static VKAPI_ATTR VkResult VKAPI_CALL EnumerateDeviceExtensionProperties(
            VkPhysicalDevice /*physical_device*/, const char* /*p_layer_name*/,
            uint32_t* p_property_count, VkExtensionProperties* /*p_properties*/) {
        *p_property_count = 0;
        return VK_SUCCESS;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice /*physical_device*/,
                                                       const VkDeviceCreateInfo* /*p_create_info*/,
                                                       const VkAllocationCallbacks* /*p_allocator*/,
                                                       VkDevice* /*p_device*/) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice /*device*/,
                                                                      const char* /*p_name*/) {
        return nullptr;
    }

    static VKAPI_ATTR VkResult VKAPI_CALL CreateRenderPass(
            VkDevice /*device*/, const VkRenderPassCreateInfo* /*p_create_info*/,
            const VkAllocationCallbacks* /*p_allocator*/, VkRenderPass* /*p_render_pass*/) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance /*instance*/,
                                                                        const char* name) {
        const std::string_view fn_name(name);
        if (fn_name == "vkDestroyInstance") {
            return reinterpret_cast<PFN_vkVoidFunction>(&DestroyInstance);
        }
        if (fn_name == "vkEnumeratePhysicalDevices") {
            return reinterpret_cast<PFN_vkVoidFunction>(&EnumeratePhysicalDevices);
        }
        if (fn_name == "vkGetPhysicalDeviceProperties") {
            return reinterpret_cast<PFN_vkVoidFunction>(&GetPhysicalDeviceProperties);
        }
        if (fn_name == "vkGetPhysicalDeviceMemoryProperties") {
            return reinterpret_cast<PFN_vkVoidFunction>(&GetPhysicalDeviceMemoryProperties);
        }
        if (fn_name == "vkGetPhysicalDeviceQueueFamilyProperties") {
            return reinterpret_cast<PFN_vkVoidFunction>(&GetPhysicalDeviceQueueFamilyProperties);
        }
        if (fn_name == "vkEnumerateDeviceExtensionProperties") {
            return reinterpret_cast<PFN_vkVoidFunction>(&EnumerateDeviceExtensionProperties);
        }
        if (fn_name == "vkCreateDevice") {
            return reinterpret_cast<PFN_vkVoidFunction>(&CreateDevice);
        }
        if (fn_name == "vkGetDeviceProcAddr") {
            return reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceProcAddr);
        }
        if (fn_name == "vkCreateRenderPass") {
            return reinterpret_cast<PFN_vkVoidFunction>(&CreateRenderPass);
        }
        return nullptr;
    }

    std::vector<FakePhysicalDevice> host_devices_;
    std::vector<FakePhysicalDevice> software_devices_;
    mutable std::atomic<size_t> instances_created_{0};
    mutable std::atomic<size_t> instances_destroyed_{0};
    mutable absl::Mutex obs_mu_;
    mutable std::string observed_vk_driver_files_ ABSL_GUARDED_BY(obs_mu_);
    mutable std::string observed_vk_icd_filenames_ ABSL_GUARDED_BY(obs_mu_);
    mutable std::string observed_vk_add_driver_files_ ABSL_GUARDED_BY(obs_mu_);
    mutable std::string observed_vk_loader_drivers_disable_ ABSL_GUARDED_BY(obs_mu_);
};

inline absl::StatusOr<std::filesystem::path> CreateFakeVulkanLibDir(
        const std::filesystem::path& lib64_dir) {
    const std::filesystem::path vulkan_dir = lib64_dir / "vulkan";
    if (auto s = android::base::file::mkdir_recursive(vulkan_dir, 0755); !s.ok()) {
        return s;
    }
    for (std::string_view icd : {
             "libkosmickrisp_icd.json",
             "MoltenVK_icd.json",
             "lvp_icd.json",
             "vk_swiftshader_icd.json",
         }) {
        if (auto s = android::base::file::touch(vulkan_dir / icd); !s.ok()) {
            return s;
        }
    }
    return vulkan_dir;
}

}  // namespace goldfish::gpu::test
