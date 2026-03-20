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

#pragma once

#include <memory>

#include "goldfish/gvk/goldfish_gvk_populate_member_pfn_visitor.h"
#include "goldfish/gvk/i_meta_loader.h"
#include "goldfish/gvk/util/get_pfn.h"

#define GOLDFISH_GVK_InstanceDispatch_FUNC_LIST(VISITOR) \
    VISITOR(vkEnumeratePhysicalDevices)                  \
    VISITOR(vkGetPhysicalDeviceProperties)               \
    VISITOR(vkGetPhysicalDeviceQueueFamilyProperties)    \
    VISITOR(vkGetPhysicalDeviceMemoryProperties)         \
    VISITOR(vkEnumerateDeviceExtensionProperties)        \
    VISITOR(vkCreateDevice)                              \
    VISITOR(vkGetDeviceProcAddr)                         \
    VISITOR(vkCreateRenderPass)

namespace goldfish::gvk {

class DeviceDispatch;
class InstanceDispatch {
  public:
    using Ptr = std::shared_ptr<const InstanceDispatch>;

    static Ptr create(const IMetaLoader::Ptr&, const VkInstanceCreateInfo&);
    static Ptr create(const IMetaLoader::Ptr&, uint32_t maxApiVersion,
                      const char* appName = nullptr, uint32_t enabledLayerCount = 0,
                      const char* const* enabledLayerNames = nullptr,
                      uint32_t enabledExtensionCount = 0,
                      const char* const* enabledExtensionNames = nullptr);

    VkResult enumeratePhysicalDevices(uint32_t* pPhysicalDeviceCount,
                                      VkPhysicalDevice* pPhysicalDevices) const;
    VkPhysicalDeviceProperties getPhysicalDeviceProperties(VkPhysicalDevice) const;
    std::vector<VkQueueFamilyProperties> getPhysicalDeviceQueueFamilyProperties(
            VkPhysicalDevice) const;
    VkPhysicalDeviceMemoryProperties getPhysicalDeviceMemoryProperties(VkPhysicalDevice) const;
    std::vector<VkExtensionProperties> enumerateDeviceExtensionProperties(
            VkPhysicalDevice, const char* layerName) const;

  private:
    friend DeviceDispatch;
    struct Private {};

    VkDevice createDevice(VkPhysicalDevice, const VkDeviceCreateInfo&) const;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr() const { return mPFN_vkGetDeviceProcAddr; }
    bool initPFNs(const util::GetPFN&);

    const IMetaLoader::Ptr mLoader;
    const VkInstance mVkInstance;
    const PFN_vkDestroyInstance mPFN_vkDestroyInstance;

    GOLDFISH_GVK_InstanceDispatch_FUNC_LIST(GOLDFISH_GVK_POPULATE_MEMBER_PFN_VISITOR);

  public:
    InstanceDispatch(IMetaLoader::Ptr, VkInstance, PFN_vkDestroyInstance, Private);
    ~InstanceDispatch();

    InstanceDispatch(const InstanceDispatch&) = delete;
    InstanceDispatch(InstanceDispatch&&) = delete;
    InstanceDispatch& operator=(const InstanceDispatch&) = delete;
    InstanceDispatch& operator=(InstanceDispatch&&) = delete;
};

}  // namespace goldfish::gvk
