/* Copyright 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "goldfish/gvk/instance_dispatch.h"

#include "absl/log/log.h"

#include "goldfish/gvk/util/init_pfn.h"
#include "goldfish/gvk/util/log_vk_result.h"

namespace goldfish::gvk {

using util::LogVkResult;

namespace {
void StubForDestroyInstance(VkInstance, const VkAllocationCallbacks*) {
    LOG(ERROR) << "If you see this function called this means the Vulkan "
                  "implementation did not provide `vkDestroyInstance`.";
}
}  // namespace

InstanceDispatch::InstanceDispatch(IMetaLoader::Ptr loader, const VkInstance instance,
                                   const PFN_vkDestroyInstance destroy_instance, Private)
        : loader_(std::move(loader))
        , instance_(instance)
        , pfn_vkDestroyInstance(destroy_instance) {}

InstanceDispatch::~InstanceDispatch() {
    (*pfn_vkDestroyInstance)(instance_, nullptr);
}

InstanceDispatch::Ptr InstanceDispatch::create(const IMetaLoader::Ptr& loader,
                                               const VkInstanceCreateInfo& instance_create_info) {
    const VkInstance instance = loader->createInstance(instance_create_info);
    if (!instance) {
        return nullptr;
    }

    const auto get_instance_proc_addr = loader->getInstanceProcAddr();
    const auto get_pfn = [get_instance_proc_addr, instance](const char* name) {
        return reinterpret_cast<void*>(get_instance_proc_addr(instance, name));
    };

    PFN_vkDestroyInstance destroy_instance;
    if (!util::initPFN(destroy_instance, get_pfn, "vkDestroyInstance",
                       "InstanceDispatch::create")) {
        destroy_instance = &StubForDestroyInstance;
        LOG(ERROR) << "No way to destroy `vkInstance` because `vkDestroyInstance` is missing.";
    }

    auto instance_dispatch =
            std::make_shared<InstanceDispatch>(loader, instance, destroy_instance, Private());
    if (instance_dispatch->initPFNs(get_pfn)) {
        return instance_dispatch;
    } else {
        return nullptr;
    }
}

InstanceDispatch::Ptr InstanceDispatch::create(const IMetaLoader::Ptr& loader,
                                               const uint32_t max_api_version, const char* app_name,
                                               const uint32_t enabled_layer_count,
                                               const char* const* enabled_layer_names,
                                               const uint32_t enabled_extension_count,
                                               const char* const* enabled_extension_names) {
    const VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = app_name,
        .applicationVersion = 0,
        .pEngineName = nullptr,
        .engineVersion = 0,
        .apiVersion = max_api_version,
    };

    const VkInstanceCreateInfo instance_create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = enabled_layer_count,
        .ppEnabledLayerNames = enabled_layer_names,
        .enabledExtensionCount = enabled_extension_count,
        .ppEnabledExtensionNames = enabled_extension_names,
    };

    return create(loader, instance_create_info);
}

bool InstanceDispatch::initPFNs(const util::GetPFN& getPFN) {
#define INIT_1_PFN(F) util::initPFN(pfn_##F, getPFN, #F, "InstanceDispatch::initPFNs") &&
    return GOLDFISH_GVK_InstanceDispatch_FUNC_LIST(INIT_1_PFN) true;
#undef INIT_1_PFN
}

/*********************************************************************************************** */

VkResult InstanceDispatch::enumeratePhysicalDevices(uint32_t* p_physical_device_count,
                                                    VkPhysicalDevice* p_physical_devices) const {
    return (*pfn_vkEnumeratePhysicalDevices)(instance_, p_physical_device_count,
                                             p_physical_devices);
}

VkPhysicalDeviceProperties InstanceDispatch::getPhysicalDeviceProperties(
        const VkPhysicalDevice dev) const {
    VkPhysicalDeviceProperties props;
    (*pfn_vkGetPhysicalDeviceProperties)(dev, &props);
    return props;
}

std::vector<VkQueueFamilyProperties> InstanceDispatch::getPhysicalDeviceQueueFamilyProperties(
        const VkPhysicalDevice dev) const {
    uint32_t queue_family_count = 0;
    (*pfn_vkGetPhysicalDeviceQueueFamilyProperties)(dev, &queue_family_count, nullptr);

    if (queue_family_count == 0) {
        return {};
    }

    std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_count);
    (*pfn_vkGetPhysicalDeviceQueueFamilyProperties)(dev, &queue_family_count,
                                                    queue_family_properties.data());

    return queue_family_properties;
}

VkPhysicalDeviceMemoryProperties InstanceDispatch::getPhysicalDeviceMemoryProperties(
        const VkPhysicalDevice dev) const {
    VkPhysicalDeviceMemoryProperties props;
    (*pfn_vkGetPhysicalDeviceMemoryProperties)(dev, &props);
    return props;
}

std::vector<VkExtensionProperties> InstanceDispatch::enumerateDeviceExtensionProperties(
        const VkPhysicalDevice dev, const char* layer_name) const {
    uint32_t property_count = 0;
    VkResult result =
            (*pfn_vkEnumerateDeviceExtensionProperties)(dev, layer_name, &property_count, nullptr);
    if (result != VK_SUCCESS) {
        LogVkResult("vkEnumerateDeviceExtensionProperties", result);
        return {};
    }

    if (property_count == 0) {
        return {};
    }

    std::vector<VkExtensionProperties> properties(property_count);
    result = (*pfn_vkEnumerateDeviceExtensionProperties)(dev, layer_name, &property_count,
                                                         properties.data());
    if (result != VK_SUCCESS) {
        LogVkResult("vkEnumerateDeviceExtensionProperties", result);
        return {};
    }

    return properties;
}

VkDevice InstanceDispatch::createDevice(const VkPhysicalDevice physical_device,
                                        const VkDeviceCreateInfo& create_info) const {
    VkDevice device = nullptr;
    const VkResult result = (*pfn_vkCreateDevice)(physical_device, &create_info, nullptr, &device);
    if (result != VK_SUCCESS) {
        LogVkResult("vkCreateDevice", result);
        return VK_NULL_HANDLE;
    }
    return device;
}

}  // namespace goldfish::gvk
