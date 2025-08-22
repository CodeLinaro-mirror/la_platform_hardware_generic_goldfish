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

#include "goldfish/gvk/util/buildDeviceQueueCreateInfo.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace goldfish::gvk::util {

namespace {
void checkCreateInfo(const std::vector<VkDeviceQueueCreateInfo>& createInfos,
                     const uint32_t familyIndex, const uint32_t expectedQueueCount) {
    auto it = std::find_if(createInfos.begin(), createInfos.end(),
                           [familyIndex](const VkDeviceQueueCreateInfo& ci) {
                               return ci.queueFamilyIndex == familyIndex;
                           });
    ASSERT_NE(it, createInfos.end());
    EXPECT_EQ(it->queueCount, expectedQueueCount);
}
}  // namespace

TEST(BuildDeviceQueueCreateInfo, Empty) {
    const VkQueueFamilyProperties qfps[] = {
        {
            .queueFlags = VK_QUEUE_GRAPHICS_BIT,
            .queueCount = 1,
        },
    };

    auto [createInfos1, locations1] = buildDeviceQueueCreateInfo(0, qfps, VK_QUEUE_GRAPHICS_BIT);
    EXPECT_TRUE(createInfos1.empty());

    auto [createInfos2, locations2] = buildDeviceQueueCreateInfo(1, qfps, 0);
    EXPECT_TRUE(createInfos2.empty());
}

TEST(BuildDeviceQueueCreateInfo, NoSolution) {
    const VkQueueFamilyProperties qfps[] = {
        {
            .queueFlags = VK_QUEUE_COMPUTE_BIT,
            .queueCount = 1,
            .minImageTransferGranularity = {1, 1, 1},
        },
    };

    // Request graphics, but only compute is available.
    const VkQueueFlags requestedFlags = VK_QUEUE_GRAPHICS_BIT;

    auto [createInfos, locations] = buildDeviceQueueCreateInfo(1, qfps, requestedFlags);

    EXPECT_TRUE(createInfos.empty());
    EXPECT_FALSE(locations.graphics.ok());
}

TEST(BuildDeviceQueueCreateInfo, UniversalQueue) {
    const VkQueueFamilyProperties qfps[] = {
        {
            .queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
            .queueCount = 4,
            .minImageTransferGranularity = {1, 1, 1},
        },
    };

    const VkQueueFlags requestedFlags =
            VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;

    auto [createInfos, locations] = buildDeviceQueueCreateInfo(1, qfps, requestedFlags);

    // We should have one create info for the single queue family.
    ASSERT_EQ(createInfos.size(), 1);
    EXPECT_EQ(createInfos[0].queueFamilyIndex, 0);
    // We requested 3 queues, and the family supports 4, so we should get 3.
    EXPECT_EQ(createInfos[0].queueCount, 3);

    // Check locations
    EXPECT_TRUE(locations.graphics.ok());
    EXPECT_EQ(locations.graphics.familyIndex, 0);
    EXPECT_EQ(locations.graphics.queueIndex, 0);  // First allocation

    EXPECT_FALSE(locations.presentation.ok());  // Not requested

    EXPECT_TRUE(locations.compute.ok());
    EXPECT_EQ(locations.compute.familyIndex, 0);
    EXPECT_EQ(locations.compute.queueIndex, 1);  // Second allocation

    EXPECT_TRUE(locations.transfer.ok());
    EXPECT_EQ(locations.transfer.familyIndex, 0);
    EXPECT_EQ(locations.transfer.queueIndex, 2);  // Third allocation
}

TEST(BuildDeviceQueueCreateInfo, UniversalQueueRoundRobin) {
    const VkQueueFamilyProperties qfps[] = {
        {
            .queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT |
                          GVK_QUEUE_PRESENTATION_BIT,
            .queueCount = 2,
            .minImageTransferGranularity = {1, 1, 1},
        },
    };

    const VkQueueFlags requestedFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT |
                                        VK_QUEUE_TRANSFER_BIT | GVK_QUEUE_PRESENTATION_BIT;

    auto [createInfos, locations] = buildDeviceQueueCreateInfo(1, qfps, requestedFlags);

    // We should have one create info for the single queue family.
    ASSERT_EQ(createInfos.size(), 1);
    EXPECT_EQ(createInfos[0].queueFamilyIndex, 0);
    // We requested 4 queue types, but graphics and presentation are shared.
    // So 3 logical queues are allocated from a family that supports 2 physical queues.
    // The implementation should create 2 physical queues and reuse them.
    EXPECT_EQ(createInfos[0].queueCount, 2);

    // Check locations (round-robin with presentation sharing graphics queue)
    // 1. Graphics is allocated.
    EXPECT_TRUE(locations.graphics.ok());
    EXPECT_EQ(locations.graphics.familyIndex, 0);
    EXPECT_EQ(locations.graphics.queueIndex, 0);  // alloc_count=0 -> 0%2=0

    // 2. Presentation shares the graphics queue.
    EXPECT_TRUE(locations.presentation.ok());
    EXPECT_EQ(locations.presentation.familyIndex, 0);
    EXPECT_EQ(locations.presentation.queueIndex, 0);

    // 3. Compute is allocated.
    EXPECT_TRUE(locations.compute.ok());
    EXPECT_EQ(locations.compute.familyIndex, 0);
    EXPECT_EQ(locations.compute.queueIndex, 1);  // alloc_count=1 -> 1%2=1

    // 4. Transfer is allocated.
    EXPECT_TRUE(locations.transfer.ok());
    EXPECT_EQ(locations.transfer.familyIndex, 0);
    EXPECT_EQ(locations.transfer.queueIndex, 0);  // alloc_count=2 -> 2%2=0
}

TEST(BuildDeviceQueueCreateInfo, SpecializedQueues) {
    const VkQueueFamilyProperties qfps[] = {
        {
            // Family 0: Compute only
            .queueFlags = VK_QUEUE_COMPUTE_BIT,
            .queueCount = 1,
            .minImageTransferGranularity = {1, 1, 1},
        },
        {
            // Family 1: Transfer only
            .queueFlags = VK_QUEUE_TRANSFER_BIT,
            .queueCount = 1,
            .minImageTransferGranularity = {1, 1, 1},
        },
        {
            // Family 2: Graphics only
            .queueFlags = VK_QUEUE_GRAPHICS_BIT,
            .queueCount = 1,
            .minImageTransferGranularity = {1, 1, 1},
        },
    };

    const VkQueueFlags requestedFlags =
            VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;

    auto [createInfos, locations] = buildDeviceQueueCreateInfo(3, qfps, requestedFlags);

    // We should have three create infos, one for each family.
    ASSERT_EQ(createInfos.size(), 3);
    checkCreateInfo(createInfos, 0, 1);
    checkCreateInfo(createInfos, 1, 1);
    checkCreateInfo(createInfos, 2, 1);

    // Check locations. The allocation logic prefers specialized queues.
    // The sorting is by popcount(queueFlags) ascending then by queueCount (descending)
    // then by the queue family index (descending).
    // All have popcount 1 and queueCount 1, so preferred order of families is 2, 1, 0.

    // Graphics is requested first. It will find family 2.
    EXPECT_TRUE(locations.graphics.ok());
    EXPECT_EQ(locations.graphics.familyIndex, 2);
    EXPECT_EQ(locations.graphics.queueIndex, 0);

    // Compute is requested next. It will find family 0.
    EXPECT_TRUE(locations.compute.ok());
    EXPECT_EQ(locations.compute.familyIndex, 0);
    EXPECT_EQ(locations.compute.queueIndex, 0);

    // Transfer is requested last. It will find family 1.
    EXPECT_TRUE(locations.transfer.ok());
    EXPECT_EQ(locations.transfer.familyIndex, 1);
    EXPECT_EQ(locations.transfer.queueIndex, 0);
}

TEST(BuildDeviceQueueCreateInfo, PresentationQueue) {
    // Case 1: Graphics queue supports presentation
    const VkQueueFamilyProperties qfps1[] = {
        {
            .queueFlags = VK_QUEUE_GRAPHICS_BIT | GVK_QUEUE_PRESENTATION_BIT,
            .queueCount = 1,
            .minImageTransferGranularity = {1, 1, 1},
        },
    };
    const VkQueueFlags requestedFlags1 = VK_QUEUE_GRAPHICS_BIT | GVK_QUEUE_PRESENTATION_BIT;
    auto [createInfos1, locations1] = buildDeviceQueueCreateInfo(1, qfps1, requestedFlags1);

    ASSERT_EQ(createInfos1.size(), 1);
    EXPECT_EQ(createInfos1[0].queueFamilyIndex, 0);
    EXPECT_EQ(createInfos1[0].queueCount, 1);  // one queue used for both
    EXPECT_TRUE(locations1.graphics.ok());
    EXPECT_EQ(locations1.graphics.familyIndex, 0);
    EXPECT_EQ(locations1.graphics.queueIndex, 0);
    EXPECT_TRUE(locations1.presentation.ok());
    // Should be the same as graphics
    EXPECT_EQ(locations1.presentation.familyIndex, locations1.graphics.familyIndex);
    EXPECT_EQ(locations1.presentation.queueIndex, locations1.graphics.queueIndex);
}

TEST(BuildDeviceQueueCreateInfo, PresentationQueueSeparate) {
    // Separate graphics and presentation queues
    const VkQueueFamilyProperties qfps[] = {
        {
            // Family 0: Graphics only
            .queueFlags = VK_QUEUE_GRAPHICS_BIT,
            .queueCount = 1,
            .minImageTransferGranularity = {1, 1, 1},
        },
        {
            // Family 1: Presentation only
            .queueFlags = GVK_QUEUE_PRESENTATION_BIT,
            .queueCount = 1,
            .minImageTransferGranularity = {1, 1, 1},
        },
    };
    const VkQueueFlags requestedFlags = VK_QUEUE_GRAPHICS_BIT | GVK_QUEUE_PRESENTATION_BIT;
    auto [createInfos, locations] = buildDeviceQueueCreateInfo(2, qfps, requestedFlags);

    ASSERT_EQ(createInfos.size(), 2);
    checkCreateInfo(createInfos, 0, 1);
    checkCreateInfo(createInfos, 1, 1);

    // Preferred order: popcount 1, queuecount 1, index desc -> 1, 0
    // Graphics requested first. Iterates 1, 0. Family 1 fails. Family 0 succeeds.
    EXPECT_TRUE(locations.graphics.ok());
    EXPECT_EQ(locations.graphics.familyIndex, 0);
    EXPECT_EQ(locations.graphics.queueIndex, 0);
    // Presentation requested next. Iterates 1, 0. Family 1 succeeds.
    EXPECT_TRUE(locations.presentation.ok());
    EXPECT_EQ(locations.presentation.familyIndex, 1);
    EXPECT_EQ(locations.presentation.queueIndex, 0);
}

TEST(BuildDeviceQueueCreateInfo, TransferGranularity) {
    const VkQueueFamilyProperties qfps[] = {
        {
            // Family 0: Transfer only, but bad granularity
            .queueFlags = VK_QUEUE_TRANSFER_BIT,
            .queueCount = 1,
            .minImageTransferGranularity = {2, 1, 1},  // width != 1
        },
        {
            // Family 1: Graphics + Transfer, good granularity
            .queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT,
            .queueCount = 1,
            .minImageTransferGranularity = {1, 1, 1},
        },
    };

    const VkQueueFlags requestedFlags = VK_QUEUE_TRANSFER_BIT;

    // The sort order will be family 0 then family 1 (popcount 1 vs 2).
    // The transfer search will check family 0 first, but reject it due to granularity.
    // Then it will check family 1 and accept it.
    auto [createInfos, locations] = buildDeviceQueueCreateInfo(2, qfps, requestedFlags);

    ASSERT_EQ(createInfos.size(), 1);
    EXPECT_EQ(createInfos[0].queueFamilyIndex, 1);
    EXPECT_EQ(createInfos[0].queueCount, 1);

    EXPECT_TRUE(locations.transfer.ok());
    EXPECT_EQ(locations.transfer.familyIndex, 1);
    EXPECT_EQ(locations.transfer.queueIndex, 0);
}

TEST(BuildDeviceQueueCreateInfo, PreferSpecialized) {
    const VkQueueFamilyProperties qfps[] = {
        {
            // Family 0: Graphics, Compute, Transfer.
            .queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT,
            .queueCount = 1,
            .minImageTransferGranularity = {1, 1, 1},
        },
        {
            // Family 1: Compute only.
            .queueFlags = VK_QUEUE_COMPUTE_BIT,
            .queueCount = 1,
            .minImageTransferGranularity = {1, 1, 1},
        },
    };

    const VkQueueFlags requestedFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;

    // Sort order: Family 1 (popcount 1), Family 0 (popcount 3)
    // Preferred order of indices: 1, 0

    auto [createInfos, locations] = buildDeviceQueueCreateInfo(2, qfps, requestedFlags);

    // Allocation:
    // 1. Graphics: search 1, 0. Family 1 fails. Family 0 succeeds.
    //    locations.graphics = {family: 0, queue: 0}. alloc_count[0] = 1.
    // 2. Compute: search 1, 0. Family 1 succeeds.
    //    locations.compute = {family: 1, queue: 0}. alloc_count[1] = 1.

    ASSERT_EQ(createInfos.size(), 2);
    checkCreateInfo(createInfos, 0, 1);
    checkCreateInfo(createInfos, 1, 1);

    EXPECT_TRUE(locations.graphics.ok());
    EXPECT_EQ(locations.graphics.familyIndex, 0);
    EXPECT_EQ(locations.graphics.queueIndex, 0);

    EXPECT_TRUE(locations.compute.ok());
    EXPECT_EQ(locations.compute.familyIndex, 1);
    EXPECT_EQ(locations.compute.queueIndex, 0);
}

}  // namespace goldfish::gvk::util