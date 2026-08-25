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

#include <cstdint>
#include <functional>

#include "goldfish/gvk/instance_dispatch.h"

namespace goldfish::gvk::util {

enum class PhysicalDeviceRejectionReason : int32_t {
    NONE,
    NOT_VULKAN,       // apiVersion has a non-zero variant
    LOW_API_VERSION,  // `apiVersion` does not meet the requirements
    MISSING_EXTENSIONS,
    SKIPPED,  // by an explicit filter, e.g. we need exactly llvmpipe
              // or exactly this `VkPhysicalDeviceProperties::pipelineCacheUUID`
    BLOCKLISTED,
};

using PhysicalDeviceScoringFunction = std::function<int32_t(const VkPhysicalDeviceProperties&)>;

VkPhysicalDevice SelectPhysicalDevice(const InstanceDispatch&, const PhysicalDeviceScoringFunction&,
                                      bool verbose);

VkPhysicalDevice SelectPhysicalDeviceByIndex(const InstanceDispatch&, size_t index);

}  // namespace goldfish::gvk::util
