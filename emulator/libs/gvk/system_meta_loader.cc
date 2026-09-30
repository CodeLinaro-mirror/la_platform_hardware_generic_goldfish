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

#include "goldfish/gvk/system_meta_loader.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

#include "absl/log/log.h"

#include "goldfish/gvk/util/init_pfn.h"
#include "goldfish/gvk/util/log_vk_result.h"

namespace goldfish::gvk {

using util::LogVkResult;

uint32_t SystemMetaLoader::enumerateInstanceVersion() const {
    uint32_t api_version = 0;
    const VkResult result = (*pfn_vkEnumerateInstanceVersion)(&api_version);
    if (result != VK_SUCCESS) {
        LogVkResult("vkEnumerateInstanceVersion", result);
        return 0U;
    }

    return api_version;
}

std::vector<VkLayerProperties> SystemMetaLoader::enumerateInstanceLayerProperties() const {
    VkResult result;
    uint32_t property_count = 0;
    result = (*pfn_vkEnumerateInstanceLayerProperties)(&property_count, nullptr);
    if (result != VK_SUCCESS) {
        LogVkResult("vkEnumerateInstanceLayerProperties", result);
        return {};
    }

    if (property_count == 0) {
        return {};
    }

    std::vector<VkLayerProperties> layer_props(property_count);
    result = (*pfn_vkEnumerateInstanceLayerProperties)(&property_count, layer_props.data());
    if (result != VK_SUCCESS) {
        LogVkResult("vkEnumerateInstanceLayerProperties", result);
        return {};
    }

    return layer_props;
}

std::vector<VkExtensionProperties> SystemMetaLoader::enumerateInstanceExtensionProperties(
        const char* layer_name) const {
    VkResult result;
    uint32_t property_count = 0;
    result = (*pfn_vkEnumerateInstanceExtensionProperties)(layer_name, &property_count, nullptr);
    if (result != VK_SUCCESS) {
        LogVkResult("vkEnumerateInstanceExtensionProperties", result);
        return {};
    }

    if (property_count == 0) {
        return {};
    }

    std::vector<VkExtensionProperties> ext_props(property_count);
    result = (*pfn_vkEnumerateInstanceExtensionProperties)(layer_name, &property_count,
                                                           ext_props.data());
    if (result != VK_SUCCESS) {
        LogVkResult("vkEnumerateInstanceExtensionProperties", result);
        return {};
    }

    return ext_props;
}

VkInstance SystemMetaLoader::createInstance(const VkInstanceCreateInfo& create_info) const {
    VkInstance instance = nullptr;
    const VkResult result = (*pfn_vkCreateInstance)(&create_info, nullptr, &instance);
    if (result != VK_SUCCESS) {
        LogVkResult("vkCreateInstance", result);
        return VK_NULL_HANDLE;
    }

    return instance;
}

PFN_vkGetInstanceProcAddr SystemMetaLoader::getInstanceProcAddr() const {
    return pfn_vkGetInstanceProcAddr;
}

IMetaLoader::Ptr SystemMetaLoader::get(const char* custom_search_dir) {
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
    auto try_load = [&loader](const char* path) -> bool {
        os::DynamicLibrary lib(path);
        if (lib.ok()) {
            if (!loader) {
                loader = std::make_shared<SystemMetaLoader>(Private());
            }

            if (loader->initPFNs([&lib](const char* name) { return lib[name]; })) {
                loader->library_ = std::move(lib);
                return true;
            }
        }
        return false;
    };

    if (custom_search_dir && custom_search_dir[0] != '\0') {
        const std::filesystem::path base_dir(custom_search_dir);
        for (const char* path : kSearchPaths) {
            const std::string full_path = (base_dir / path).string();
            if (try_load(full_path.c_str())) {
                return loader;
            }
        }
    }

    for (const char* path : kSearchPaths) {
        if (try_load(path)) {
            return loader;
        }
    }

    LOG(ERROR) << "Can't find a Vulkan implementation";
    return nullptr;
}

bool SystemMetaLoader::initPFNs(const util::GetPFN& getPFN) {
#define INIT_1_PFN(F) util::initPFN(pfn_##F, getPFN, #F, "SystemMetaLoader::initPFNs") &&
    return GOLDFISH_GVK_SystemMetaLoader_FUNC_LIST(INIT_1_PFN) true;
#undef INIT_1_PFN
}

}  // namespace goldfish::gvk
