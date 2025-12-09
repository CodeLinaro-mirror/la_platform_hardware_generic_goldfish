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
std::vector<uint16_t> getQueueFamilyPreferredOrder(const VkQueueFamilyProperties* qfps,
                                                   const size_t qfpsSize) {
    std::vector<uint16_t> queueFamilyIndices(qfpsSize);
    for (size_t i = 0; i < qfpsSize; ++i) {
        queueFamilyIndices[i] = i;
    }

    std::sort(queueFamilyIndices.begin(), queueFamilyIndices.end(),
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

    return queueFamilyIndices;
}

void setDeviceQueueLocation(DeviceQueueLocation& dst, const unsigned queueFamilyIndex,
                            const unsigned queueFamilyCapacity,
                            uint16_t queueFamilyAllocCounter[]) {
    dst.familyIndex = queueFamilyIndex;
    dst.queueIndex = queueFamilyAllocCounter[queueFamilyIndex] % queueFamilyCapacity;
    ++queueFamilyAllocCounter[queueFamilyIndex];
}

VkDeviceQueueCreateInfo makeDQCI(const uint32_t queueFamilyIndex, const uint32_t queueCount) {
    return {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = queueFamilyIndex,
        .queueCount = queueCount,
        .pQueuePriorities = nullptr,
    };
}
}  // namespace

BuildDeviceQueueCreateInfoResult buildDeviceQueueCreateInfo(
        const size_t qfpsSize, const VkQueueFamilyProperties* qfps,
        const VkQueueFlags requestedQueueFlags) {
    if (!qfpsSize || !requestedQueueFlags) {
        return {};
    }

    BuildDeviceQueueCreateInfoResult result;

    const std::vector<uint16_t> queueFamilyPreferredOrder =
            getQueueFamilyPreferredOrder(qfps, qfpsSize);

    std::vector<uint16_t> queueFamilyAllocCounter(qfpsSize);

    if (requestedQueueFlags & VK_QUEUE_GRAPHICS_BIT) {
        for (unsigned i = 0; i < qfpsSize; ++i) {
            const unsigned queueFamilyIndex = queueFamilyPreferredOrder[i];
            const VkQueueFamilyProperties& qfp = qfps[queueFamilyIndex];

            if ((qfp.queueFlags & VK_QUEUE_GRAPHICS_BIT) && (qfp.queueCount > 0)) {
                setDeviceQueueLocation(result.second.graphics, queueFamilyIndex, qfp.queueCount,
                                       queueFamilyAllocCounter.data());

                // if presentation is requested and the graphics queue supports it
                // then reuse the graphics queue for presentation.
                if (requestedQueueFlags & qfp.queueFlags & GVK_QUEUE_PRESENTATION_BIT) {
                    result.second.presentation = result.second.graphics;
                }
                break;
            }
        }

        if (!result.second.graphics.ok()) {
            return {};
        }
    }

    if ((requestedQueueFlags & GVK_QUEUE_PRESENTATION_BIT) && !result.second.presentation.ok()) {
        for (unsigned i = 0; i < qfpsSize; ++i) {
            const unsigned queueFamilyIndex = queueFamilyPreferredOrder[i];
            const VkQueueFamilyProperties& qfp = qfps[queueFamilyIndex];

            if ((qfp.queueFlags & GVK_QUEUE_PRESENTATION_BIT) && (qfp.queueCount > 0)) {
                if (queueFamilyIndex == result.second.graphics.familyIndex) {
                    result.second.presentation = result.second.graphics;
                } else {
                    setDeviceQueueLocation(result.second.presentation, queueFamilyIndex,
                                           qfp.queueCount, queueFamilyAllocCounter.data());
                }
            }
        }

        if (!result.second.presentation.ok()) {
            return {};
        }
    }

    if (requestedQueueFlags & VK_QUEUE_COMPUTE_BIT) {
        for (unsigned i = 0; i < qfpsSize; ++i) {
            const unsigned queueFamilyIndex = queueFamilyPreferredOrder[i];
            const VkQueueFamilyProperties& qfp = qfps[queueFamilyIndex];

            if ((qfp.queueFlags & VK_QUEUE_COMPUTE_BIT) && (qfp.queueCount > 0)) {
                setDeviceQueueLocation(result.second.compute, queueFamilyIndex, qfp.queueCount,
                                       queueFamilyAllocCounter.data());
                break;
            }
        }

        if (!result.second.compute.ok()) {
            return {};
        }
    }

    if (requestedQueueFlags & VK_QUEUE_TRANSFER_BIT) {
        for (unsigned i = 0; i < qfpsSize; ++i) {
            const unsigned queueFamilyIndex = queueFamilyPreferredOrder[i];
            const VkQueueFamilyProperties& qfp = qfps[queueFamilyIndex];

            if ((qfp.queueFlags & VK_QUEUE_TRANSFER_BIT) && (qfp.queueCount > 0)) {
                const VkExtent3D& mtg = qfp.minImageTransferGranularity;

                if ((mtg.width == 1) && (mtg.width == mtg.height) && (mtg.depth == 1)) {
                    setDeviceQueueLocation(result.second.transfer, queueFamilyIndex, qfp.queueCount,
                                           queueFamilyAllocCounter.data());
                    break;
                }
            }
        }

        if (!result.second.transfer.ok()) {
            return {};
        }
    }

    for (unsigned queueFamilyIndex = 0; queueFamilyIndex < qfpsSize; ++queueFamilyIndex) {
        const uint32_t allocCount = queueFamilyAllocCounter[queueFamilyIndex];
        if (allocCount > 0) {
            result.first.push_back(makeDQCI(
                    queueFamilyIndex, std::min(allocCount, qfps[queueFamilyIndex].queueCount)));
        }
    }

    return result;
}

}  // namespace goldfish::gvk::util
