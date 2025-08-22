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

#include "goldfish/gvk/SystemMetaLoader.h"

#include <memory>
#include <mutex>

#include "absl/log/log.h"

#include "goldfish/gvk/util/initPFN.h"
#include "goldfish/gvk/util/logVkResult.h"

namespace goldfish::gvk {

using util::logVkResult;

uint32_t SystemMetaLoader::enumerateInstanceVersion() const {
    uint32_t apiVersion = 0;
    const VkResult result = (*mPFN_vkEnumerateInstanceVersion)(&apiVersion);
    if (result != VK_SUCCESS) {
        logVkResult("vkEnumerateInstanceVersion", result);
        return 0U;
    }

    return apiVersion;
}

std::vector<VkLayerProperties> SystemMetaLoader::enumerateInstanceLayerProperties() const {
    VkResult result;
    uint32_t propertyCount = 0;
    result = (*mPFN_vkEnumerateInstanceLayerProperties)(&propertyCount, nullptr);
    if (result != VK_SUCCESS) {
        logVkResult("vkEnumerateInstanceLayerProperties", result);
        return {};
    }

    if (propertyCount == 0) {
        return {};
    }

    std::vector<VkLayerProperties> layerProps(propertyCount);
    result = (*mPFN_vkEnumerateInstanceLayerProperties)(&propertyCount, layerProps.data());
    if (result != VK_SUCCESS) {
        logVkResult("vkEnumerateInstanceLayerProperties", result);
        return {};
    }

    return layerProps;
}

std::vector<VkExtensionProperties> SystemMetaLoader::enumerateInstanceExtensionProperties(
        const char* layerName) const {
    VkResult result;
    uint32_t propertyCount = 0;
    result = (*mPFN_vkEnumerateInstanceExtensionProperties)(layerName, &propertyCount, nullptr);
    if (result != VK_SUCCESS) {
        logVkResult("vkEnumerateInstanceExtensionProperties", result);
        return {};
    }

    if (propertyCount == 0) {
        return {};
    }

    std::vector<VkExtensionProperties> extProps(propertyCount);
    result = (*mPFN_vkEnumerateInstanceExtensionProperties)(layerName, &propertyCount,
                                                            extProps.data());
    if (result != VK_SUCCESS) {
        logVkResult("vkEnumerateInstanceExtensionProperties", result);
        return {};
    }

    return extProps;
}

VkInstance SystemMetaLoader::createInstance(const VkInstanceCreateInfo& createInfo) const {
    VkInstance instance = nullptr;
    const VkResult result = (*mPFN_vkCreateInstance)(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        logVkResult("vkCreateInstance", result);
        return VK_NULL_HANDLE;
    }

    return instance;
}

PFN_vkGetInstanceProcAddr SystemMetaLoader::getInstanceProcAddr() const {
    return mPFN_vkGetInstanceProcAddr;
}

IMetaLoader::Ptr SystemMetaLoader::get() {
    static const char* const kSearchPaths[] = {
#if defined(__linux__)
        "libvulkan.so.1",
        "libvulkan.so",
#elif defined(__APPLE__)
        "libvulkan.dylib",         "libvulkan.1.dylib",           "libMoltenVK.dylib",
        "vulkan.framework/vulkan", "MoltenVK.framework/MoltenVK",
#elif defined(_WIN32)
        "vulkan-1.dll",
#else
#error unknown OS
#endif
    };

    std::shared_ptr<SystemMetaLoader> loader;
    for (const char* path : kSearchPaths) {
        os::DynamicLibrary lib(path);
        if (lib.ok()) {
            if (!loader) {
                loader = std::make_shared<SystemMetaLoader>(Private());
            }

            if (loader->initPFNs([&lib](const char* name) { return lib[name]; })) {
                loader->mLib = std::move(lib);
                return loader;
            }
        }
    }

    LOG(ERROR) << "Can't find a Vulkan implementation";
    return nullptr;
}

bool SystemMetaLoader::initPFNs(const util::GetPFN& getPFN) {
#define INIT_1_PFN(F) util::initPFN(mPFN_##F, getPFN, #F, "SystemMetaLoader::initPFNs") &&
    return GOLDFISH_GVK_SystemMetaLoader_FUNC_LIST(INIT_1_PFN) true;
#undef INIT_1_PFN
}

}  // namespace goldfish::gvk
