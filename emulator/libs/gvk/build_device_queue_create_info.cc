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

#include "goldfish/gvk/util/build_device_queue_create_info.h"

#include <algorithm>
#include <bit>
#include <map>
#include <vector>

namespace goldfish::gvk::util {
namespace {
std::vector<uint16_t> GetQueueFamilyPreferredOrder(const VkQueueFamilyProperties* qfps,
                                                   const size_t qfps_size) {
    std::vector<uint16_t> queue_family_indices(qfps_size);
    for (size_t i = 0; i < qfps_size; ++i) {
        queue_family_indices[i] = i;
    }

    std::sort(queue_family_indices.begin(), queue_family_indices.end(),
              [qfps](const unsigned lhsi, const unsigned rhsi) {
                  const VkQueueFamilyProperties& lhs = qfps[lhsi];
                  const VkQueueFamilyProperties& rhs = qfps[rhsi];

                  const unsigned lhspc = std::popcount(lhs.queueFlags);
                  const unsigned rhspc = std::popcount(rhs.queueFlags);

                  // sort by the number of bits in queueFlags (ascending) then
                  // by queueCount (descending) then by the queue family index (descending).
                  if (lhspc < rhspc) {
                      return true;
                  } else if (lhspc > rhspc) {
                      return false;
                  } else if (lhs.queueCount > rhs.queueCount) {
                      return true;
                  } else if (lhs.queueCount < rhs.queueCount) {
                      return false;
                  } else {
                      return lhsi > rhsi;
                  }
              });

    return queue_family_indices;
}

void SetDeviceQueueLocation(DeviceQueueLocation& dst, const unsigned queue_family_index,
                            const unsigned queue_family_capacity,
                            uint16_t queue_family_alloc_counter[]) {
    dst.familyIndex = queue_family_index;
    dst.queueIndex = queue_family_alloc_counter[queue_family_index] % queue_family_capacity;
    ++queue_family_alloc_counter[queue_family_index];
}

VkDeviceQueueCreateInfo MakeDeviceQueueCreateInfo(const uint32_t queue_family_index,
                                                  const uint32_t queue_count) {
    return {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = queue_family_index,
        .queueCount = queue_count,
        .pQueuePriorities = nullptr,
    };
}
}  // namespace

BuildDeviceQueueCreateInfoResult BuildDeviceQueueCreateInfo(
        const size_t qfps_size, const VkQueueFamilyProperties* qfps,
        const VkQueueFlags requested_queue_flags) {
    if (!qfps_size || !requested_queue_flags) {
        return {};
    }

    BuildDeviceQueueCreateInfoResult result;

    const std::vector<uint16_t> queue_family_preferred_order =
            GetQueueFamilyPreferredOrder(qfps, qfps_size);

    std::vector<uint16_t> queue_family_alloc_counter(qfps_size);

    if (requested_queue_flags & VK_QUEUE_GRAPHICS_BIT) {
        for (unsigned i = 0; i < qfps_size; ++i) {
            const unsigned queue_family_index = queue_family_preferred_order[i];
            const VkQueueFamilyProperties& qfp = qfps[queue_family_index];

            if ((qfp.queueFlags & VK_QUEUE_GRAPHICS_BIT) && (qfp.queueCount > 0)) {
                SetDeviceQueueLocation(result.second.graphics, queue_family_index, qfp.queueCount,
                                       queue_family_alloc_counter.data());

                // if presentation is requested and the graphics queue supports it
                // then reuse the graphics queue for presentation.
                if (requested_queue_flags & qfp.queueFlags & GVK_QUEUE_PRESENTATION_BIT) {
                    result.second.presentation = result.second.graphics;
                }
                break;
            }
        }

        if (!result.second.graphics.ok()) {
            return {};
        }
    }

    if ((requested_queue_flags & GVK_QUEUE_PRESENTATION_BIT) && !result.second.presentation.ok()) {
        for (unsigned i = 0; i < qfps_size; ++i) {
            const unsigned queue_family_index = queue_family_preferred_order[i];
            const VkQueueFamilyProperties& qfp = qfps[queue_family_index];

            if ((qfp.queueFlags & GVK_QUEUE_PRESENTATION_BIT) && (qfp.queueCount > 0)) {
                if (queue_family_index == result.second.graphics.familyIndex) {
                    result.second.presentation = result.second.graphics;
                } else {
                    SetDeviceQueueLocation(result.second.presentation, queue_family_index,
                                           qfp.queueCount, queue_family_alloc_counter.data());
                }
            }
        }

        if (!result.second.presentation.ok()) {
            return {};
        }
    }

    if (requested_queue_flags & VK_QUEUE_COMPUTE_BIT) {
        for (unsigned i = 0; i < qfps_size; ++i) {
            const unsigned queue_family_index = queue_family_preferred_order[i];
            const VkQueueFamilyProperties& qfp = qfps[queue_family_index];

            if ((qfp.queueFlags & VK_QUEUE_COMPUTE_BIT) && (qfp.queueCount > 0)) {
                SetDeviceQueueLocation(result.second.compute, queue_family_index, qfp.queueCount,
                                       queue_family_alloc_counter.data());
                break;
            }
        }

        if (!result.second.compute.ok()) {
            return {};
        }
    }

    if (requested_queue_flags & VK_QUEUE_TRANSFER_BIT) {
        for (unsigned i = 0; i < qfps_size; ++i) {
            const unsigned queue_family_index = queue_family_preferred_order[i];
            const VkQueueFamilyProperties& qfp = qfps[queue_family_index];

            if ((qfp.queueFlags & VK_QUEUE_TRANSFER_BIT) && (qfp.queueCount > 0)) {
                const VkExtent3D& mtg = qfp.minImageTransferGranularity;

                if ((mtg.width == 1) && (mtg.width == mtg.height) && (mtg.depth == 1)) {
                    SetDeviceQueueLocation(result.second.transfer, queue_family_index,
                                           qfp.queueCount, queue_family_alloc_counter.data());
                    break;
                }
            }
        }

        if (!result.second.transfer.ok()) {
            return {};
        }
    }

    for (unsigned queue_family_index = 0; queue_family_index < qfps_size; ++queue_family_index) {
        const uint32_t alloc_count = queue_family_alloc_counter[queue_family_index];
        if (alloc_count > 0) {
            result.first.push_back(MakeDeviceQueueCreateInfo(
                    queue_family_index,
                    std::min(alloc_count, qfps[queue_family_index].queueCount)));
        }
    }

    return result;
}

}  // namespace goldfish::gvk::util
