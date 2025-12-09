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

#include <vulkan/vulkan.h>

#include "goldfish/gvk/device_dispatch.h"
#include "goldfish/gvk/device_resources.h"

namespace goldfish::gvk {
class DeviceDispatch;
}  // namespace goldfish::gvk

namespace goldfish::gvk::util {

std::tuple<DeviceMemory, Buffer, DeviceMemory, Buffer> createStagingBuffers(
        const gvk::DeviceDispatch& dd, const VkPhysicalDeviceMemoryProperties& memoryProperties,
        const void* data, size_t dataSize, VkBufferUsageFlags dstUsage);

std::pair<DeviceMemory, Buffer> allocateBuffer(
        const gvk::DeviceDispatch& dd, const VkPhysicalDeviceMemoryProperties& memoryProperties,
        size_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memoryPropertyFlags);

std::pair<DeviceMemory, Image> allocateImage(
        const gvk::DeviceDispatch& dd, const VkPhysicalDeviceMemoryProperties& memoryProperties,
        const VkImageCreateInfo&, VkMemoryPropertyFlags memoryPropertyFlags);

}  // namespace goldfish::gvk::util
