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
#include <vector>

#include "goldfish/gvk/DeviceQueueLocation.h"

namespace goldfish::gvk::util {

// extends VkQueueFlagBits
constexpr uint32_t GVK_QUEUE_PRESENTATION_BIT = 1U << 31;

using BuildDeviceQueueCreateInfoResult =
    std::pair<std::vector<VkDeviceQueueCreateInfo>, goldfish::gvk::DeviceQueueLocations>;

/*
 * Builds a set of `VkDeviceQueueCreateInfo` structures and maps logical queue
 * types to their physical locations. This is a helper function to simplify
 * the process of creating a `VkDevice` with the desired queue capabilities.
 *
 * The function attempts to satisfy the `requestedQueueFlags` by finding the
 * most specialized queue families available on the physical device. It
 * prioritizes queue families with fewer capabilities to avoid using a
 * universal (graphics + compute + transfer) queue for a simple transfer
 * operation if a dedicated transfer queue is available.
 *
 * If a graphics queue supports presentation and the presentation features
 * is requested (via `GVK_QUEUE_PRESENTATION_BIT`), the function reuses the
 * graphics queue.
 *
 * If several queues land into the same family and the family supports more
 * than one queue, queues are placed in the round-robin way into the
 * physical queues.
 *
 * The `qfps` array is returned by `vkGetPhysicalDeviceQueueFamilyProperties`.
 *
 * The array of `VkDeviceQueueCreateInfo` should be fed into
 * `VkDeviceCreateInfo::pQueueCreateInfos`.
 *
 *`DeviceQueueLocations` should be used with `vkGetDeviceQueue`.
 *
 * NOTE: `VkDeviceQueueCreateInfo::pQueuePriorities` is NOT initialized.
 *
 * See buildDeviceQueueCreateInfo_test.cpp for the examples.
 */
BuildDeviceQueueCreateInfoResult buildDeviceQueueCreateInfo(size_t qfpsSize,
                                                            const VkQueueFamilyProperties* qfps,
                                                            const VkQueueFlags requestedQueueFlags);

}  // namespace goldfish::gvk::util
