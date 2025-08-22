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

#include "goldfish/gvk/InstanceDispatch.h"

#include "absl/log/log.h"

#include "goldfish/gvk/util/initPFN.h"
#include "goldfish/gvk/util/logVkResult.h"

namespace goldfish::gvk {

using util::logVkResult;

namespace {
void stubForDestroyInstance(VkInstance, const VkAllocationCallbacks*) {
    LOG(ERROR) << "If you see this function called this means the Vulkan "
                  "implementation did not provide `vkDestroyInstance`.";
}
}  // namespace

InstanceDispatch::InstanceDispatch(IMetaLoader::Ptr loader, const VkInstance instance,
                                   const PFN_vkDestroyInstance destroyInstance, Private)
        : mLoader(std::move(loader))
        , mVkInstance(instance)
        , mPFN_vkDestroyInstance(destroyInstance) {}

InstanceDispatch::~InstanceDispatch() {
    (*mPFN_vkDestroyInstance)(mVkInstance, nullptr);
}

InstanceDispatch::Ptr InstanceDispatch::create(const IMetaLoader::Ptr& loader,
                                               const VkInstanceCreateInfo& instanceCreateInfo) {
    const VkInstance instance = loader->createInstance(instanceCreateInfo);
    if (!instance) {
        return nullptr;
    }

    const auto getInstanceProcAddr = loader->getInstanceProcAddr();
    const auto getPFN = [getInstanceProcAddr, instance](const char* name) {
        return reinterpret_cast<void*>(getInstanceProcAddr(instance, name));
    };

    PFN_vkDestroyInstance destroyInstance;
    if (!util::initPFN(destroyInstance, getPFN, "vkDestroyInstance", "InstanceDispatch::create")) {
        destroyInstance = &stubForDestroyInstance;
        LOG(ERROR) << "No way to destroy `vkInstance` because `vkDestroyInstance` is missing.";
    }

    auto instanceDispatch =
            std::make_shared<InstanceDispatch>(loader, instance, destroyInstance, Private());
    if (instanceDispatch->initPFNs(getPFN)) {
        return instanceDispatch;
    } else {
        return nullptr;
    }
}

InstanceDispatch::Ptr InstanceDispatch::create(const IMetaLoader::Ptr& loader,
                                               const uint32_t maxApiVersion, const char* appName,
                                               const uint32_t enabledLayerCount,
                                               const char* const* enabledLayerNames,
                                               const uint32_t enabledExtensionCount,
                                               const char* const* enabledExtensionNames) {
    const VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = appName,
        .applicationVersion = 0,
        .pEngineName = nullptr,
        .engineVersion = 0,
        .apiVersion = maxApiVersion,
    };

    const VkInstanceCreateInfo instanceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = enabledLayerCount,
        .ppEnabledLayerNames = enabledLayerNames,
        .enabledExtensionCount = enabledExtensionCount,
        .ppEnabledExtensionNames = enabledExtensionNames,
    };

    return create(loader, instanceCreateInfo);
}

bool InstanceDispatch::initPFNs(const util::GetPFN& getPFN) {
#define INIT_1_PFN(F) util::initPFN(mPFN_##F, getPFN, #F, "InstanceDispatch::initPFNs") &&
    return GOLDFISH_GVK_InstanceDispatch_FUNC_LIST(INIT_1_PFN) true;
#undef INIT_1_PFN
}

/*********************************************************************************************** */

VkResult InstanceDispatch::enumeratePhysicalDevices(uint32_t* pPhysicalDeviceCount,
                                                    VkPhysicalDevice* pPhysicalDevices) const {
    return (*mPFN_vkEnumeratePhysicalDevices)(mVkInstance, pPhysicalDeviceCount, pPhysicalDevices);
}

VkPhysicalDeviceProperties InstanceDispatch::getPhysicalDeviceProperties(
        const VkPhysicalDevice dev) const {
    VkPhysicalDeviceProperties props;
    (*mPFN_vkGetPhysicalDeviceProperties)(dev, &props);
    return props;
}

std::vector<VkQueueFamilyProperties> InstanceDispatch::getPhysicalDeviceQueueFamilyProperties(
        const VkPhysicalDevice dev) const {
    uint32_t queueFamilyCount = 0;
    (*mPFN_vkGetPhysicalDeviceQueueFamilyProperties)(dev, &queueFamilyCount, nullptr);

    if (queueFamilyCount == 0) {
        return {};
    }

    std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
    (*mPFN_vkGetPhysicalDeviceQueueFamilyProperties)(dev, &queueFamilyCount,
                                                     queueFamilyProperties.data());

    return queueFamilyProperties;
}

VkPhysicalDeviceMemoryProperties InstanceDispatch::getPhysicalDeviceMemoryProperties(
        const VkPhysicalDevice dev) const {
    VkPhysicalDeviceMemoryProperties props;
    (*mPFN_vkGetPhysicalDeviceMemoryProperties)(dev, &props);
    return props;
}

std::vector<VkExtensionProperties> InstanceDispatch::enumerateDeviceExtensionProperties(
        const VkPhysicalDevice dev, const char* layerName) const {
    uint32_t propertyCount = 0;
    VkResult result =
            (*mPFN_vkEnumerateDeviceExtensionProperties)(dev, layerName, &propertyCount, nullptr);
    if (result != VK_SUCCESS) {
        logVkResult("vkEnumerateDeviceExtensionProperties", result);
        return {};
    }

    if (propertyCount == 0) {
        return {};
    }

    std::vector<VkExtensionProperties> properties(propertyCount);
    result = (*mPFN_vkEnumerateDeviceExtensionProperties)(dev, layerName, &propertyCount,
                                                          properties.data());
    if (result != VK_SUCCESS) {
        logVkResult("vkEnumerateDeviceExtensionProperties", result);
        return {};
    }

    return properties;
}

VkDevice InstanceDispatch::createDevice(const VkPhysicalDevice physicalDevice,
                                        const VkDeviceCreateInfo& createInfo) const {
    VkDevice device = nullptr;
    const VkResult result = (*mPFN_vkCreateDevice)(physicalDevice, &createInfo, nullptr, &device);
    if (result != VK_SUCCESS) {
        logVkResult("vkCreateDevice", result);
        return VK_NULL_HANDLE;
    }
    return device;
}

}  // namespace goldfish::gvk
