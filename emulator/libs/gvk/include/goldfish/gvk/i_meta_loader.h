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

#include <vulkan/vulkan.h>

#include <memory>
#include <vector>

namespace goldfish::gvk {

class InstanceDispatch;

class IMetaLoader {
  public:
    using Ptr = std::shared_ptr<const IMetaLoader>;

    virtual ~IMetaLoader() = default;

    virtual uint32_t enumerateInstanceVersion() const = 0;
    virtual std::vector<VkLayerProperties> enumerateInstanceLayerProperties() const = 0;
    virtual std::vector<VkExtensionProperties> enumerateInstanceExtensionProperties(
            const char* layerName) const = 0;

  private:
    friend InstanceDispatch;

    virtual VkInstance createInstance(const VkInstanceCreateInfo&) const = 0;
    virtual PFN_vkGetInstanceProcAddr getInstanceProcAddr() const = 0;
};

}  // namespace goldfish::gvk
