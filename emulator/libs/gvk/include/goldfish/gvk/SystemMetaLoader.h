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

#include "goldfish/gvk/GOLDFISH_GVK_POPULATE_MEMBER_PFN_VISITOR.h"
#include "goldfish/gvk/IMetaLoader.h"
#include "goldfish/gvk/util/GetPFN.h"
#include "goldfish/os/DynamicLibrary.h"

#define GOLDFISH_GVK_SystemMetaLoader_FUNC_LIST(VISITOR) \
    VISITOR(vkEnumerateInstanceVersion)                  \
    VISITOR(vkEnumerateInstanceLayerProperties)          \
    VISITOR(vkEnumerateInstanceExtensionProperties)      \
    VISITOR(vkCreateInstance)                            \
    VISITOR(vkGetInstanceProcAddr)

namespace goldfish::gvk {

class SystemMetaLoader : public IMetaLoader {
  public:
    static IMetaLoader::Ptr get();

    uint32_t enumerateInstanceVersion() const override;
    std::vector<VkLayerProperties> enumerateInstanceLayerProperties() const override;
    std::vector<VkExtensionProperties> enumerateInstanceExtensionProperties(
            const char* layerName) const override;

  private:
    struct Private {};

    bool initPFNs(const util::GetPFN&);

    PFN_vkGetInstanceProcAddr getInstanceProcAddr() const override;
    VkInstance createInstance(const VkInstanceCreateInfo&) const override;

    os::DynamicLibrary mLib;

    GOLDFISH_GVK_SystemMetaLoader_FUNC_LIST(GOLDFISH_GVK_POPULATE_MEMBER_PFN_VISITOR);

  public:
    SystemMetaLoader(Private) {}
};

}  // namespace goldfish::gvk
