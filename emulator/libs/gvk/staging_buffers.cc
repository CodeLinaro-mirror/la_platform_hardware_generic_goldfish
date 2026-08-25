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
        const gvk::DeviceDispatch& dd, const VkPhysicalDeviceMemoryProperties& memory_properties,
        const void* data, const size_t data_size, const VkBufferUsageFlags dst_usage) {
    DeviceMemory local_src_mem;  // to call ~Buffer before ~DeviceMemory
    Buffer local_src_buf = dd.createBuffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (!local_src_buf) {
        return {};
    }

    const VkMemoryRequirements src_mem_reqs = dd.getBufferMemoryRequirements(local_src_buf.get());
    const int src_memory_type_index = GetMemoryTypeIndex(
            memory_properties, src_mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (src_memory_type_index < 0) {
        return {};
    }

    local_src_mem = dd.allocateMemory(src_mem_reqs.size, src_memory_type_index);
    if (!local_src_mem) {
        return {};
    }

    void* mapped_data = dd.mapMemory(local_src_mem.get(), data_size);
    if (!mapped_data) {
        return {};
    }
    memcpy(mapped_data, data, data_size);
    dd.unmapMemory(local_src_mem.get());

    if (!dd.bindBufferMemory(local_src_buf.get(), local_src_mem.get(), 0)) {
        return {};
    }

    DeviceMemory local_dst_mem;  // to call ~Buffer before ~DeviceMemory
    Buffer local_dst_buf = dd.createBuffer(data_size, dst_usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    if (!local_dst_buf) {
        return {};
    }

    const VkMemoryRequirements dst_mem_reqs = dd.getBufferMemoryRequirements(local_dst_buf.get());
    const int dst_memory_type_index = GetMemoryTypeIndex(
            memory_properties, dst_mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (dst_memory_type_index < 0) {
        return {};
    }

    local_dst_mem = dd.allocateMemory(dst_mem_reqs.size, dst_memory_type_index);
    if (!local_dst_mem) {
        return {};
    }

    if (!dd.bindBufferMemory(local_dst_buf.get(), local_dst_mem.get(), 0)) {
        return {};
    }

    return {std::move(local_src_mem), std::move(local_src_buf), std::move(local_dst_mem),
            std::move(local_dst_buf)};
}

std::pair<DeviceMemory, Buffer> allocateBuffer(
        const gvk::DeviceDispatch& dd, const VkPhysicalDeviceMemoryProperties& memory_properties,
        const size_t size, const VkBufferUsageFlags usage,
        const VkMemoryPropertyFlags memory_property_flags) {
    Buffer buffer = dd.createBuffer(size, usage);
    if (!buffer) {
        return {};
    }

    const VkMemoryRequirements mem_reqs = dd.getBufferMemoryRequirements(buffer.get());
    const int memory_type_index =
            GetMemoryTypeIndex(memory_properties, mem_reqs.memoryTypeBits, memory_property_flags);
    if (memory_type_index < 0) {
        return {};
    }

    DeviceMemory memory = dd.allocateMemory(mem_reqs.size, memory_type_index);
    if (!memory) {
        return {};
    }

    if (!dd.bindBufferMemory(buffer.get(), memory.get(), 0)) {
        return {};
    }

    return {std::move(memory), std::move(buffer)};
}

std::pair<DeviceMemory, Image> allocateImage(
        const gvk::DeviceDispatch& dd, const VkPhysicalDeviceMemoryProperties& memory_properties,
        const VkImageCreateInfo& create_info, VkMemoryPropertyFlags memory_property_flags) {
    Image image = dd.createImage(create_info);
    if (!image) {
        return {};
    }

    const VkMemoryRequirements mem_reqs = dd.getImageMemoryRequirements(image.get());
    const int memory_type_index =
            GetMemoryTypeIndex(memory_properties, mem_reqs.memoryTypeBits, memory_property_flags);
    if (memory_type_index < 0) {
        return {};
    }

    DeviceMemory memory = dd.allocateMemory(mem_reqs.size, memory_type_index);
    if (!memory) {
        return {};
    }

    if (!dd.bindImageMemory(image.get(), memory.get(), 0)) {
        return {};
    }

    return {std::move(memory), std::move(image)};
}

}  // namespace goldfish::gvk::util