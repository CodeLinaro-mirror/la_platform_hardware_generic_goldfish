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

#include "goldfish/gvk/goldfish/gvk/util/staging_buffers.h"

#include <string.h>

#include "goldfish/gvk/device_dispatch.h"
#include "goldfish/gvk/util/get_memory_type_index.h"

namespace goldfish::gvk::util {

std::tuple<DeviceMemory, Buffer, DeviceMemory, Buffer> createStagingBuffers(
        const gvk::DeviceDispatch& dd, const VkPhysicalDeviceMemoryProperties& memoryProperties,
        const void* data, const size_t dataSize, const VkBufferUsageFlags dstUsage) {
    DeviceMemory localSrcMem;  // to call ~Buffer before ~DeviceMemory
    Buffer localSrcBuf = dd.createBuffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (!localSrcBuf) {
        return {};
    }

    const VkMemoryRequirements srcMemReqs = dd.getBufferMemoryRequirements(localSrcBuf.get());
    const int srcMemoryTypeIndex = getMemoryTypeIndex(memoryProperties, srcMemReqs.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (srcMemoryTypeIndex < 0) {
        return {};
    }

    localSrcMem = dd.allocateMemory(srcMemReqs.size, srcMemoryTypeIndex);
    if (!localSrcMem) {
        return {};
    }

    void* mappedData = dd.mapMemory(localSrcMem.get(), dataSize);
    if (!mappedData) {
        return {};
    }
    memcpy(mappedData, data, dataSize);
    dd.unmapMemory(localSrcMem.get());

    if (!dd.bindBufferMemory(localSrcBuf.get(), localSrcMem.get(), 0)) {
        return {};
    }

    DeviceMemory localDstMem;  // to call ~Buffer before ~DeviceMemory
    Buffer localDstBuf = dd.createBuffer(dataSize, dstUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    if (!localDstBuf) {
        return {};
    }

    const VkMemoryRequirements dstMemReqs = dd.getBufferMemoryRequirements(localDstBuf.get());
    const int dstMemoryTypeIndex = getMemoryTypeIndex(memoryProperties, dstMemReqs.memoryTypeBits,
                                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (dstMemoryTypeIndex < 0) {
        return {};
    }

    localDstMem = dd.allocateMemory(dstMemReqs.size, dstMemoryTypeIndex);
    if (!localDstMem) {
        return {};
    }

    if (!dd.bindBufferMemory(localDstBuf.get(), localDstMem.get(), 0)) {
        return {};
    }

    return {std::move(localSrcMem), std::move(localSrcBuf), std::move(localDstMem),
            std::move(localDstBuf)};
}

std::pair<DeviceMemory, Buffer> allocateBuffer(
        const gvk::DeviceDispatch& dd, const VkPhysicalDeviceMemoryProperties& memoryProperties,
        const size_t size, const VkBufferUsageFlags usage,
        const VkMemoryPropertyFlags memoryPropertyFlags) {
    Buffer buffer = dd.createBuffer(size, usage);
    if (!buffer) {
        return {};
    }

    const VkMemoryRequirements memReqs = dd.getBufferMemoryRequirements(buffer.get());
    const int memoryTypeIndex =
            getMemoryTypeIndex(memoryProperties, memReqs.memoryTypeBits, memoryPropertyFlags);
    if (memoryTypeIndex < 0) {
        return {};
    }

    DeviceMemory memory = dd.allocateMemory(memReqs.size, memoryTypeIndex);
    if (!memory) {
        return {};
    }

    if (!dd.bindBufferMemory(buffer.get(), memory.get(), 0)) {
        return {};
    }

    return {std::move(memory), std::move(buffer)};
}

std::pair<DeviceMemory, Image> allocateImage(
        const gvk::DeviceDispatch& dd, const VkPhysicalDeviceMemoryProperties& memoryProperties,
        const VkImageCreateInfo& createInfo, VkMemoryPropertyFlags memoryPropertyFlags) {
    Image image = dd.createImage(createInfo);
    if (!image) {
        return {};
    }

    const VkMemoryRequirements memReqs = dd.getImageMemoryRequirements(image.get());
    const int memoryTypeIndex =
            getMemoryTypeIndex(memoryProperties, memReqs.memoryTypeBits, memoryPropertyFlags);
    if (memoryTypeIndex < 0) {
        return {};
    }

    DeviceMemory memory = dd.allocateMemory(memReqs.size, memoryTypeIndex);
    if (!memory) {
        return {};
    }

    if (!dd.bindImageMemory(image.get(), memory.get(), 0)) {
        return {};
    }

    return {std::move(memory), std::move(image)};
}

}  // namespace goldfish::gvk::util