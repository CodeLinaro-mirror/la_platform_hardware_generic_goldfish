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
    VkPhysicalDeviceMemoryProperties mem_props = {};
    mem_props.memoryTypeCount = 2;
    mem_props.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    mem_props.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    const uint32_t supported_types = 1 << 1;  // Support type at index 1
    const VkMemoryPropertyFlags required_props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    EXPECT_EQ(GetMemoryTypeIndex(mem_props, supported_types, required_props), 1);
}

TEST(GetMemoryTypeIndex, NoSupportedType) {
    VkPhysicalDeviceMemoryProperties mem_props = {};
    mem_props.memoryTypeCount = 2;
    mem_props.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    mem_props.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    const uint32_t supported_types = 0;  // Support no types
    const VkMemoryPropertyFlags required_props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    EXPECT_EQ(GetMemoryTypeIndex(mem_props, supported_types, required_props), -1);
}

TEST(GetMemoryTypeIndex, NoMatchingProperties) {
    VkPhysicalDeviceMemoryProperties mem_props = {};
    mem_props.memoryTypeCount = 2;
    mem_props.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    mem_props.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    const uint32_t supported_types = 1 << 0;  // Support type at index 0
    const VkMemoryPropertyFlags required_props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    EXPECT_EQ(GetMemoryTypeIndex(mem_props, supported_types, required_props), -1);
}

TEST(GetMemoryTypeIndex, MultipleMatches) {
    VkPhysicalDeviceMemoryProperties mem_props = {};
    mem_props.memoryTypeCount = 4;
    mem_props.memoryTypes[0].propertyFlags = 0;
    mem_props.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    mem_props.memoryTypes[2].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    mem_props.memoryTypes[3].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    const uint32_t supported_types = 0b1110;  // Support types 1, 2, 3
    const VkMemoryPropertyFlags required_props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

    // Should return the first match, which is at index 1.
    EXPECT_EQ(GetMemoryTypeIndex(mem_props, supported_types, required_props), 1);
}

TEST(GetMemoryTypeIndex, MultipleRequiredProperties) {
    VkPhysicalDeviceMemoryProperties mem_props = {};
    mem_props.memoryTypeCount = 4;
    mem_props.memoryTypes[0].propertyFlags = 0;
    mem_props.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    mem_props.memoryTypes[2].propertyFlags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    mem_props.memoryTypes[3].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    const uint32_t supported_types = 0b1110;  // Support types 1, 2, 3
    const VkMemoryPropertyFlags required_props =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    // Type 1 is supported but doesn't have HOST_COHERENT.
    // Type 2 is supported and has both required properties.
    EXPECT_EQ(GetMemoryTypeIndex(mem_props, supported_types, required_props), 2);
}

TEST(GetMemoryTypeIndex, NoMemoryTypes) {
    VkPhysicalDeviceMemoryProperties mem_props = {};
    mem_props.memoryTypeCount = 0;

    const uint32_t supported_types = 0b1;
    const VkMemoryPropertyFlags required_props = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    EXPECT_EQ(GetMemoryTypeIndex(mem_props, supported_types, required_props), -1);
}

}  // namespace goldfish::gvk::util
