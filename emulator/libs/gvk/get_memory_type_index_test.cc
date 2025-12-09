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

#include "goldfish/gvk/util/get_memory_type_index.h"

#include <gtest/gtest.h>

namespace goldfish::gvk::util {

TEST(GetMemoryTypeIndex, Success) {
    VkPhysicalDeviceMemoryProperties memProps = {};
    memProps.memoryTypeCount = 2;
    memProps.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    memProps.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    const uint32_t supportedTypes = 1 << 1;  // Support type at index 1
    const VkMemoryPropertyFlags requiredProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    EXPECT_EQ(getMemoryTypeIndex(memProps, supportedTypes, requiredProps), 1);
}

TEST(GetMemoryTypeIndex, NoSupportedType) {
    VkPhysicalDeviceMemoryProperties memProps = {};
    memProps.memoryTypeCount = 2;
    memProps.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    memProps.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    const uint32_t supportedTypes = 0;  // Support no types
    const VkMemoryPropertyFlags requiredProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    EXPECT_EQ(getMemoryTypeIndex(memProps, supportedTypes, requiredProps), -1);
}

TEST(GetMemoryTypeIndex, NoMatchingProperties) {
    VkPhysicalDeviceMemoryProperties memProps = {};
    memProps.memoryTypeCount = 2;
    memProps.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    memProps.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    const uint32_t supportedTypes = 1 << 0;  // Support type at index 0
    const VkMemoryPropertyFlags requiredProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    EXPECT_EQ(getMemoryTypeIndex(memProps, supportedTypes, requiredProps), -1);
}

TEST(GetMemoryTypeIndex, MultipleMatches) {
    VkPhysicalDeviceMemoryProperties memProps = {};
    memProps.memoryTypeCount = 4;
    memProps.memoryTypes[0].propertyFlags = 0;
    memProps.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    memProps.memoryTypes[2].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    memProps.memoryTypes[3].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    const uint32_t supportedTypes = 0b1110;  // Support types 1, 2, 3
    const VkMemoryPropertyFlags requiredProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

    // Should return the first match, which is at index 1.
    EXPECT_EQ(getMemoryTypeIndex(memProps, supportedTypes, requiredProps), 1);
}

TEST(GetMemoryTypeIndex, MultipleRequiredProperties) {
    VkPhysicalDeviceMemoryProperties memProps = {};
    memProps.memoryTypeCount = 4;
    memProps.memoryTypes[0].propertyFlags = 0;
    memProps.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    memProps.memoryTypes[2].propertyFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    memProps.memoryTypes[3].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    const uint32_t supportedTypes = 0b1110;  // Support types 1, 2, 3
    const VkMemoryPropertyFlags requiredProps =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    // Type 1 is supported but doesn't have HOST_COHERENT.
    // Type 2 is supported and has both required properties.
    EXPECT_EQ(getMemoryTypeIndex(memProps, supportedTypes, requiredProps), 2);
}

TEST(GetMemoryTypeIndex, NoMemoryTypes) {
    VkPhysicalDeviceMemoryProperties memProps = {};
    memProps.memoryTypeCount = 0;

    const uint32_t supportedTypes = 0b1;
    const VkMemoryPropertyFlags requiredProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    EXPECT_EQ(getMemoryTypeIndex(memProps, supportedTypes, requiredProps), -1);
}

}  // namespace goldfish::gvk::util
