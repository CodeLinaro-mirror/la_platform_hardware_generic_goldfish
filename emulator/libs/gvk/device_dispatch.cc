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

#define FAILURE_DEBUG_PREFIX "DeviceDispatch"

#include "goldfish/gvk/device_dispatch.h"

#include <algorithm>
#include <numeric>

#include "absl/log/log.h"

#include "goldfish/base/array_size.h"
#include "goldfish/debug.h"
#include "goldfish/gvk/util/build_device_queue_create_info.h"
#include "goldfish/gvk/util/init_pfn.h"
#include "goldfish/gvk/util/log_vk_result.h"

namespace goldfish::gvk {
using util::LogVkResult;

namespace {
void StubForDestroyDevice(VkDevice, const VkAllocationCallbacks*) {
    LOG(ERROR) << "If you see this function called this means the Vulkan "
                  "implementation did not provide `vkDestroyDevice`.";
}
}  // namespace

DeviceDispatch::DeviceDispatch(InstanceDispatch::Ptr instance_dispatch, const VkDevice device,
                               const PFN_vkDestroyDevice destroy_device, Private)
        : instance_dispatch_(std::move(instance_dispatch))
        , device_(device)
        , pfn_vkDestroyDevice(destroy_device) {}

DeviceDispatch::~DeviceDispatch() {
    (*pfn_vkDestroyDevice)(device_, nullptr);
}

DeviceDispatch::Ptr DeviceDispatch::create(const InstanceDispatch::Ptr& instance_dispatch,
                                           const VkPhysicalDevice physical_device,
                                           const VkDeviceCreateInfo& create_info) {
    const VkDevice device = instance_dispatch->createDevice(physical_device, create_info);
    if (!device) {
        return FAILURE(nullptr);
    }

    const auto get_device_proc_addr = instance_dispatch->getDeviceProcAddr();
    const auto get_pfn = [get_device_proc_addr, device](const char* name) {
        return reinterpret_cast<void*>(get_device_proc_addr(device, name));
    };

    PFN_vkDestroyDevice destroy_device;
    if (!util::initPFN(destroy_device, get_pfn, "vkDestroyDevice", "DeviceDispatch::create")) {
        destroy_device = &StubForDestroyDevice;
        LOG(ERROR) << "No way to destroy `vkDevice` because `vkDestroyDevice` is missing.";
    }

    auto device_dispatch =
            std::make_shared<DeviceDispatch>(instance_dispatch, device, destroy_device, Private());
    if (device_dispatch->initPFNs(get_pfn)) {
        return device_dispatch;
    } else {
        return FAILURE(nullptr);
    }
}

std::optional<DeviceDispatch::CreateResult> DeviceDispatch::create(
        const InstanceDispatch::Ptr& instance_dispatch, const VkPhysicalDevice dev,
        const VkQueueFlags queue_flags0, const VkSurfaceKHR surface,
        const uint32_t enabled_extension_count, const char* const* enabled_extension_names,
        const VkPhysicalDeviceFeatures2* features2) {
    const VkQueueFlags queue_flags =
            queue_flags0 | (surface ? util::GVK_QUEUE_PRESENTATION_BIT : 0);

    std::vector<VkQueueFamilyProperties> queue_family_properties =
            instance_dispatch->getPhysicalDeviceQueueFamilyProperties(dev);

    auto [deviceQueueCreateInfos, deviceQueueLocations] = util::BuildDeviceQueueCreateInfo(
            queue_family_properties.size(), queue_family_properties.data(), queue_flags);
    if (deviceQueueCreateInfos.empty()) {
        return FAILURE(std::nullopt);
    }

    const uint32_t max_queue_prio_count = std::accumulate(
            deviceQueueCreateInfos.begin(), deviceQueueCreateInfos.end(), static_cast<uint32_t>(0),
            [](const uint32_t max_so_far, const VkDeviceQueueCreateInfo& dqci) {
                return std::max(max_so_far, dqci.queueCount);
            });

    static const float kGraphicsQueuePriorities8[] = {1.0F, 1.0F, 1.0F, 1.0F,
                                                      1.0F, 1.0F, 1.0F, 1.0F};

    std::vector<float> graphics_queue_priorities_vector;
    const float* graphics_queue_priorities;
    if (max_queue_prio_count <= ARRAY_SIZE(kGraphicsQueuePriorities8)) {
        graphics_queue_priorities = kGraphicsQueuePriorities8;
    } else {
        graphics_queue_priorities_vector.resize(max_queue_prio_count, kGraphicsQueuePriorities8[0]);
        graphics_queue_priorities = graphics_queue_priorities_vector.data();
    }

    for (auto& qci : deviceQueueCreateInfos) {
        qci.pQueuePriorities = graphics_queue_priorities;
    }

    const VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = features2,
        .flags = 0,
        .queueCreateInfoCount = static_cast<uint32_t>(deviceQueueCreateInfos.size()),
        .pQueueCreateInfos = deviceQueueCreateInfos.data(),
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = enabled_extension_count,
        .ppEnabledExtensionNames = enabled_extension_names,
        .pEnabledFeatures = nullptr,
    };

    Ptr dispatch = DeviceDispatch::create(instance_dispatch, dev, device_create_info);
    if (!dispatch) {
        return FAILURE(std::nullopt);
    }

    CreateResult result = {
        .dispatch = std::move(dispatch),
        .queueLocations = deviceQueueLocations,
    };

    return result;
}

bool DeviceDispatch::initPFNs(const util::GetPFN& getPFN) {
#define INIT_1_PFN(F) util::initPFN(pfn_##F, getPFN, #F, "DeviceDispatch::initPFNs") &&
    return GOLDFISH_GVK_DeviceDispatch_FUNC_LIST(INIT_1_PFN) true;
#undef INIT_1_PFN
}

/**************************************************************************************************/

VkQueue DeviceDispatch::getDeviceQueue(const uint32_t queue_family_index,
                                       const uint32_t queue_index) const {
    VkQueue queue = VK_NULL_HANDLE;
    (*pfn_vkGetDeviceQueue)(device_, queue_family_index, queue_index, &queue);
    return queue;
}

DeviceMemory DeviceDispatch::allocateMemory(const size_t allocation_size,
                                            const uint32_t memory_type_index) const {
    const VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = static_cast<VkDeviceSize>(allocation_size),
        .memoryTypeIndex = memory_type_index,
    };

    VkDeviceMemory memory = VK_NULL_HANDLE;
    const VkResult result = (*pfn_vkAllocateMemory)(device_, &alloc_info, nullptr, &memory);
    if (result != VK_SUCCESS) {
        LogVkResult("vkAllocateMemory", result);
        return {};
    }

    return DeviceMemory(memory, DeviceResourceDeleter(this));
}

void* DeviceDispatch::mapMemory(const VkDeviceMemory memory, const size_t size, const size_t offset,
                                const VkMemoryMapFlags flags) const {
    void* data = nullptr;
    const VkResult result = (*pfn_vkMapMemory)(device_, memory, offset, size, flags, &data);
    if (result != VK_SUCCESS) {
        LogVkResult("vkMapMemory", result);
        return nullptr;
    }

    return data;
}

void DeviceDispatch::unmapMemory(const VkDeviceMemory memory) const {
    (*pfn_vkUnmapMemory)(device_, memory);
}

void DeviceDispatch::freeMemory(VkDeviceMemory memory) const {
    (*pfn_vkFreeMemory)(device_, memory, nullptr);
}

Buffer DeviceDispatch::createBuffer(const size_t size, const VkBufferUsageFlags usage,
                                    const VkBufferCreateFlags flags,
                                    const uint32_t* queue_family_indices,
                                    const uint32_t queue_family_indices_size) const {
    const VkBufferCreateInfo buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = flags,
        .size = static_cast<VkDeviceSize>(size),
        .usage = usage,
        .sharingMode = (queue_family_indices_size > 0) ? VK_SHARING_MODE_CONCURRENT
                                                       : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = queue_family_indices_size,
        .pQueueFamilyIndices = queue_family_indices,
    };

    VkBuffer buffer = VK_NULL_HANDLE;
    VkResult result = (*pfn_vkCreateBuffer)(device_, &buffer_create_info, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        LogVkResult("vkCreateBuffer", result);
        return {};
    }

    return Buffer(buffer, DeviceResourceDeleter(this));
}

VkMemoryRequirements DeviceDispatch::getBufferMemoryRequirements(const VkBuffer buffer) const {
    VkMemoryRequirements mem_reqs;
    (*pfn_vkGetBufferMemoryRequirements)(device_, buffer, &mem_reqs);
    return mem_reqs;
}

bool DeviceDispatch::bindBufferMemory(const VkBuffer buffer, const VkDeviceMemory memory,
                                      const size_t offset) const {
    const VkResult result = (*pfn_vkBindBufferMemory)(device_, buffer, memory, offset);
    if (result != VK_SUCCESS) {
        LogVkResult("vkBindBufferMemory", result);
        return false;
    }
    return true;
}

void DeviceDispatch::destroyBuffer(const VkBuffer buffer) const {
    (*pfn_vkDestroyBuffer)(device_, buffer, nullptr);
}

CommandPool DeviceDispatch::createCommandPool(const VkCommandPoolCreateFlags flags,
                                              const uint32_t queue_family_index) const {
    const VkCommandPoolCreateInfo cpci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = flags,
        .queueFamilyIndex = queue_family_index,
    };

    VkCommandPool command_pool = VK_NULL_HANDLE;
    const VkResult result = (*pfn_vkCreateCommandPool)(device_, &cpci, nullptr, &command_pool);
    if (result != VK_SUCCESS) {
        LogVkResult("vkCreateCommandPool", result);
        return {};
    }

    return CommandPool(command_pool, DeviceResourceDeleter(this));
}

void DeviceDispatch::destroyCommandPool(VkCommandPool command_pool) const {
    (*pfn_vkDestroyCommandPool)(device_, command_pool, nullptr);
}

bool DeviceDispatch::allocateCommandBuffers(const VkCommandPool command_pool,
                                            const VkCommandBufferLevel level,
                                            const uint32_t bufs_size, VkCommandBuffer* bufs) const {
    const VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = command_pool,
        .level = level,
        .commandBufferCount = bufs_size,
    };

    const VkResult result = (*pfn_vkAllocateCommandBuffers)(device_, &cbai, bufs);
    if (result != VK_SUCCESS) {
        LogVkResult("vkAllocateCommandBuffers", result);
        return false;
    }

    return true;
}

void DeviceDispatch::freeCommandBuffers(const VkCommandPool command_pool, const uint32_t bufs_size,
                                        const VkCommandBuffer* bufs) const {
    (*pfn_vkFreeCommandBuffers)(device_, command_pool, bufs_size, bufs);
}

CommandBufferEnder DeviceDispatch::beginCommandBuffer(
        const VkCommandBuffer cmdbuf, const VkCommandBufferUsageFlags usage_flags,
        const bool occlusion_query_enable, const VkQueryControlFlags query_flags,
        const VkQueryPipelineStatisticFlags pipeline_statistics) const {
    const VkCommandBufferInheritanceInfo cbii = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .pNext = nullptr,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
        .framebuffer = VK_NULL_HANDLE,
        .occlusionQueryEnable = (occlusion_query_enable ? VK_TRUE : VK_FALSE),
        .queryFlags = query_flags,
        .pipelineStatistics = pipeline_statistics,
    };

    const VkCommandBufferBeginInfo cbbi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = usage_flags,
        .pInheritanceInfo = &cbii,
    };

    const VkResult result = (*pfn_vkBeginCommandBuffer)(cmdbuf, &cbbi);
    if (result != VK_SUCCESS) {
        LogVkResult("vkBeginCommandBuffer", result);
        return {};
    }

    return CommandBufferEnder(cmdbuf, CommandBufferEnderImpl(pfn_vkEndCommandBuffer));
}

void DeviceDispatch::cmdCopyBuffer(const VkCommandBuffer command_buffer, const VkBuffer src,
                                   const VkBuffer dst, size_t size) const {
    const VkBufferCopy region = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size = static_cast<VkDeviceSize>(size),
    };

    (*pfn_vkCmdCopyBuffer)(command_buffer, src, dst, 1, &region);
}

bool DeviceDispatch::queueSubmit(VkQueue queue, const uint32_t submit_count,
                                 const VkSubmitInfo* submits, const VkFence fence) const {
    const VkResult result = (*pfn_vkQueueSubmit)(queue, submit_count, submits, fence);
    if (result != VK_SUCCESS) {
        LogVkResult("vkQueueSubmit", result);
        return false;
    }

    return true;
}

bool DeviceDispatch::queueSubmit(const VkQueue queue, const VkCommandBuffer command_buffer,
                                 const VkFence fence) const {
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };
    return queueSubmit(queue, 1, &submit_info, fence);
}

bool DeviceDispatch::queueWaitIdle(const VkQueue queue) const {
    const VkResult result = (*pfn_vkQueueWaitIdle)(queue);
    if (result != VK_SUCCESS) {
        LogVkResult("vkQueueWaitIdle", result);
        return false;
    }

    return true;
}

ShaderModule DeviceDispatch::createShaderModule(const void* code, const size_t code_size) const {
    const VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = code_size,
        .pCode = static_cast<const uint32_t*>(code),
    };

    VkShaderModule shader_module = VK_NULL_HANDLE;
    const VkResult result =
            (*pfn_vkCreateShaderModule)(device_, &create_info, nullptr, &shader_module);
    if (result != VK_SUCCESS) {
        LogVkResult("vkCreateShaderModule", result);
        return {};
    }

    return ShaderModule(shader_module, DeviceResourceDeleter(this));
}

void DeviceDispatch::destroyShaderModule(const VkShaderModule shader_module) const {
    (*pfn_vkDestroyShaderModule)(device_, shader_module, nullptr);
}

DescriptorSetLayout DeviceDispatch::createDescriptorSetLayout(
        const VkDescriptorSetLayoutCreateInfo& create_info) const {
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    const VkResult result =
            (*pfn_vkCreateDescriptorSetLayout)(device_, &create_info, nullptr, &layout);
    if (result != VK_SUCCESS) {
        LogVkResult("vkCreateDescriptorSetLayout", result);
        return {};
    }

    return DescriptorSetLayout(layout, DeviceResourceDeleter(this));
}

DescriptorSetLayout DeviceDispatch::createDescriptorSetLayout(
        const uint32_t binding_count, const VkDescriptorSetLayoutBinding* bindings,
        const VkDescriptorSetLayoutCreateFlags flags) const {
    const VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = flags,
        .bindingCount = binding_count,
        .pBindings = bindings,
    };

    return createDescriptorSetLayout(create_info);
}

void DeviceDispatch::destroyDescriptorSetLayout(const VkDescriptorSetLayout layout) const {
    (*pfn_vkDestroyDescriptorSetLayout)(device_, layout, nullptr);
}

DescriptorPool DeviceDispatch::createDescriptorPool(VkDescriptorPoolCreateFlags, uint32_t max_sets,
                                                    uint32_t pool_size_count,
                                                    const VkDescriptorPoolSize* pool_sizes) const {
    const VkDescriptorPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .maxSets = max_sets,
        .poolSizeCount = pool_size_count,
        .pPoolSizes = pool_sizes,
    };

    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    const VkResult result =
            (*pfn_vkCreateDescriptorPool)(device_, &create_info, nullptr, &descriptor_pool);
    if (result != VK_SUCCESS) {
        LogVkResult("vkCreateDescriptorPool", result);
        return {};
    }

    return DescriptorPool(descriptor_pool, DeviceResourceDeleter(this));
}

void DeviceDispatch::destroyDescriptorPool(VkDescriptorPool descriptor_pool) const {
    (*pfn_vkDestroyDescriptorPool)(device_, descriptor_pool, nullptr);
}

bool DeviceDispatch::allocateDescriptorSets(const VkDescriptorPool descriptor_pool,
                                            const uint32_t descriptor_set_count,
                                            const VkDescriptorSetLayout* set_layouts,
                                            VkDescriptorSet* descriptor_sets) const {
    const VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = descriptor_pool,
        .descriptorSetCount = descriptor_set_count,
        .pSetLayouts = set_layouts,
    };

    const VkResult result = (*pfn_vkAllocateDescriptorSets)(device_, &alloc_info, descriptor_sets);
    if (result != VK_SUCCESS) {
        LogVkResult("vkAllocateDescriptorSets", result);
        return false;
    }

    return true;
}

void DeviceDispatch::updateDescriptorSets(const uint32_t descriptor_write_count,
                                          const VkWriteDescriptorSet* descriptor_writes,
                                          const uint32_t descriptor_copy_count,
                                          const VkCopyDescriptorSet* descriptor_copies) const {
    (*pfn_vkUpdateDescriptorSets)(device_, descriptor_write_count, descriptor_writes,
                                  descriptor_copy_count, descriptor_copies);
}

bool DeviceDispatch::freeDescriptorSets(const VkDescriptorPool descriptor_pool,
                                        const uint32_t descriptor_set_count,
                                        const VkDescriptorSet* descriptor_sets) const {
    const VkResult result = (*pfn_vkFreeDescriptorSets)(device_, descriptor_pool,
                                                        descriptor_set_count, descriptor_sets);
    if (result != VK_SUCCESS) {
        LogVkResult("vkFreeDescriptorSets", result);
        return false;
    }

    return true;
}

Image DeviceDispatch::createImage(const VkImageCreateInfo& create_info) const {
    VkImage image = VK_NULL_HANDLE;
    const VkResult result = (*pfn_vkCreateImage)(device_, &create_info, nullptr, &image);
    if (result != VK_SUCCESS) {
        LogVkResult("vkCreateImage", result);
        return {};
    }

    return Image(image, DeviceResourceDeleter(this));
}

VkMemoryRequirements DeviceDispatch::getImageMemoryRequirements(const VkImage image) const {
    VkMemoryRequirements mem_reqs;
    (*pfn_vkGetImageMemoryRequirements)(device_, image, &mem_reqs);
    return mem_reqs;
}

bool DeviceDispatch::bindImageMemory(const VkImage image, const VkDeviceMemory memory,
                                     const size_t offset) const {
    const VkResult result = (*pfn_vkBindImageMemory)(device_, image, memory, offset);
    if (result != VK_SUCCESS) {
        LogVkResult("vkBindImageMemory", result);
        return false;
    }

    return true;
}

void DeviceDispatch::destroyImage(const VkImage image) const {
    (*pfn_vkDestroyImage)(device_, image, nullptr);
}

ImageView DeviceDispatch::createImageView(const VkImageViewCreateInfo& create_info) const {
    VkImageView image_view = VK_NULL_HANDLE;
    const VkResult result = (*pfn_vkCreateImageView)(device_, &create_info, nullptr, &image_view);
    if (result != VK_SUCCESS) {
        LogVkResult("vkCreateImageView", result);
        return {};
    }

    return ImageView(image_view, DeviceResourceDeleter(this));
}

ImageView DeviceDispatch::createImageView(const VkImage image, const VkImageViewType view_type,
                                          const VkFormat format,
                                          const VkImageAspectFlags aspect_flags) const {
    const VkImageViewCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = image,
        .viewType = view_type,
        .format = format,
        .components = {},  // zero-initialize to VK_COMPONENT_SWIZZLE_IDENTITY
        .subresourceRange =
                {
                    .aspectMask = aspect_flags,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
    };

    return createImageView(create_info);
}

void DeviceDispatch::destroyImageView(const VkImageView image_view) const {
    (*pfn_vkDestroyImageView)(device_, image_view, nullptr);
}

RenderPass DeviceDispatch::createRenderPass(const VkRenderPassCreateInfo& rpci) const {
    VkRenderPass render_pass = VK_NULL_HANDLE;
    const VkResult result = (*pfn_vkCreateRenderPass)(device_, &rpci, nullptr, &render_pass);
    if (result != VK_SUCCESS) {
        LogVkResult("vkCreateRenderPass", result);
        return {};
    }

    return RenderPass(render_pass, DeviceResourceDeleter(this));
}

void DeviceDispatch::destroyRenderPass(const VkRenderPass render_pass) const {
    (*pfn_vkDestroyRenderPass)(device_, render_pass, nullptr);
}

Framebuffer DeviceDispatch::createFramebuffer(const VkFramebufferCreateInfo& create_info) const {
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    const VkResult result =
            (*pfn_vkCreateFramebuffer)(device_, &create_info, nullptr, &framebuffer);
    if (result != VK_SUCCESS) {
        LogVkResult("vkCreateFramebuffer", result);
        return {};
    }

    return Framebuffer(framebuffer, DeviceResourceDeleter(this));
}

Framebuffer DeviceDispatch::createFramebuffer(const VkRenderPass render_pass,
                                              const uint32_t attachment_count,
                                              const VkImageView* attachments, const uint32_t width,
                                              const uint32_t height, const uint32_t layers) const {
    const VkFramebufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderPass = render_pass,
        .attachmentCount = attachment_count,
        .pAttachments = attachments,
        .width = width,
        .height = height,
        .layers = layers,
    };

    return createFramebuffer(create_info);
}

Framebuffer DeviceDispatch::createFramebuffer(const VkRenderPass render_pass,
                                              const VkImageView attachment, const uint32_t width,
                                              const uint32_t height, const uint32_t layers) const {
    return createFramebuffer(render_pass, 1, &attachment, width, height, layers);
}

void DeviceDispatch::destroyFramebuffer(const VkFramebuffer framebuffer) const {
    (*pfn_vkDestroyFramebuffer)(device_, framebuffer, nullptr);
}

PipelineLayout DeviceDispatch::createPipelineLayout(
        const VkPipelineLayoutCreateInfo& create_info) const {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    const VkResult result = (*pfn_vkCreatePipelineLayout)(device_, &create_info, nullptr, &layout);
    if (result != VK_SUCCESS) {
        LogVkResult("vkCreatePipelineLayout", result);
        return {};
    }

    return PipelineLayout(layout, DeviceResourceDeleter(this));
}

PipelineLayout DeviceDispatch::createPipelineLayout(
        const uint32_t set_layout_count, const VkDescriptorSetLayout* set_layouts,
        const uint32_t push_constant_range_count,
        const VkPushConstantRange* push_constant_ranges) const {
    const VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = set_layout_count,
        .pSetLayouts = set_layouts,
        .pushConstantRangeCount = push_constant_range_count,
        .pPushConstantRanges = push_constant_ranges,
    };

    return createPipelineLayout(create_info);
}

void DeviceDispatch::destroyPipelineLayout(const VkPipelineLayout pipeline_layout) const {
    (*pfn_vkDestroyPipelineLayout)(device_, pipeline_layout, nullptr);
}

bool DeviceDispatch::createGraphicsPipelinesImpl(uint32_t create_info_count,
                                                 const VkGraphicsPipelineCreateInfo* create_infos,
                                                 VkPipeline* pipelines_tmp,
                                                 Pipeline* pipelines) const {
    const VkResult result = (*pfn_vkCreateGraphicsPipelines)(
            device_, VK_NULL_HANDLE, create_info_count, create_infos, nullptr, pipelines_tmp);
    if (result != VK_SUCCESS) {
        LogVkResult("vkCreateGraphicsPipelines", result);
        return false;
    }

    for (; create_info_count > 0; --create_info_count, ++pipelines_tmp, ++pipelines) {
        *pipelines = Pipeline(*pipelines_tmp, DeviceResourceDeleter(this));
    }

    return true;
}

bool DeviceDispatch::createGraphicsPipelines(const uint32_t create_info_count,
                                             const VkGraphicsPipelineCreateInfo* create_infos,
                                             Pipeline* pipelines) const {
    constexpr size_t kSmallSize = 8;
    if (create_info_count <= kSmallSize) {
        VkPipeline pipelines_tmp[kSmallSize];
        return createGraphicsPipelinesImpl(create_info_count, create_infos, pipelines_tmp,
                                           pipelines);
    } else {
        std::vector<VkPipeline> pipelines_tmp(create_info_count);
        return createGraphicsPipelinesImpl(create_info_count, create_infos, pipelines_tmp.data(),
                                           pipelines);
    }

    return true;
}

void DeviceDispatch::destroyPipeline(const VkPipeline pipeline) const {
    (*pfn_vkDestroyPipeline)(device_, pipeline, nullptr);
}

RenderPassEnder DeviceDispatch::cmdBeginRenderPass(const VkCommandBuffer command_buffer,
                                                   const VkRenderPass render_pass,
                                                   const VkFramebuffer framebuffer,
                                                   const VkRect2D& render_area,
                                                   const uint32_t clear_value_count,
                                                   const VkClearValue* clear_values,
                                                   const VkSubpassContents subpass_contents) const {
    const VkRenderPassBeginInfo render_pass_begin_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext = nullptr,
        .renderPass = render_pass,
        .framebuffer = framebuffer,
        .renderArea = render_area,
        .clearValueCount = clear_value_count,
        .pClearValues = clear_values,
    };

    (*pfn_vkCmdBeginRenderPass)(command_buffer, &render_pass_begin_info, subpass_contents);

    return RenderPassEnder(command_buffer, RenderPassEnderImpl(pfn_vkCmdEndRenderPass));
}

void DeviceDispatch::cmdBindPipeline(const VkCommandBuffer command_buffer,
                                     const VkPipelineBindPoint bind_point,
                                     const VkPipeline pipeline) const {
    (*pfn_vkCmdBindPipeline)(command_buffer, bind_point, pipeline);
}

void DeviceDispatch::cmdBindDescriptorSets(const VkCommandBuffer command_buffer,
                                           const VkPipelineBindPoint pipeline_bind_point,
                                           const VkPipelineLayout layout, const uint32_t first_set,
                                           const uint32_t descriptor_set_count,
                                           const VkDescriptorSet* descriptor_sets,
                                           const uint32_t dynamic_offset_count,
                                           const uint32_t* dynamic_offsets) const {
    (*pfn_vkCmdBindDescriptorSets)(command_buffer, pipeline_bind_point, layout, first_set,
                                   descriptor_set_count, descriptor_sets, dynamic_offset_count,
                                   dynamic_offsets);
}

void DeviceDispatch::cmdBindVertexBuffers(const VkCommandBuffer command_buffer,
                                          const uint32_t first_binding,
                                          const uint32_t binding_count, const VkBuffer* buffers,
                                          const VkDeviceSize* offsets) const {
    (*pfn_vkCmdBindVertexBuffers)(command_buffer, first_binding, binding_count, buffers, offsets);
}

void DeviceDispatch::cmdBindIndexBuffer(const VkCommandBuffer command_buffer, const VkBuffer buffer,
                                        const VkDeviceSize offset,
                                        const VkIndexType index_type) const {
    (*pfn_vkCmdBindIndexBuffer)(command_buffer, buffer, offset, index_type);
}

void DeviceDispatch::cmdDrawIndexed(const VkCommandBuffer command_buffer,
                                    const uint32_t index_count, const uint32_t instance_count,
                                    const uint32_t first_index, const int32_t vertex_offset,
                                    uint32_t first_instance) const {
    (*pfn_vkCmdDrawIndexed)(command_buffer, index_count, instance_count, first_index, vertex_offset,
                            first_instance);
}

void DeviceDispatch::cmdBlitImage(const VkCommandBuffer command_buffer, const VkImage src_image,
                                  const VkImageLayout src_image_layout, const VkImage dst_image,
                                  const VkImageLayout dst_image_layout, const uint32_t region_count,
                                  const VkImageBlit* regions, const VkFilter filter) const {
    (*pfn_vkCmdBlitImage)(command_buffer, src_image, src_image_layout, dst_image, dst_image_layout,
                          region_count, regions, filter);
}

void DeviceDispatch::cmdCopyImageToBuffer(const VkCommandBuffer command_buffer,
                                          const VkImage src_image,
                                          const VkImageLayout src_image_layout,
                                          const VkBuffer dst_buffer, const uint32_t region_count,
                                          const VkBufferImageCopy* regions) const {
    (*pfn_vkCmdCopyImageToBuffer)(command_buffer, src_image, src_image_layout, dst_buffer,
                                  region_count, regions);
}

void DeviceDispatch::cmdPipelineBarrier(
        VkCommandBuffer command_buffer, VkPipelineStageFlags src_stage_mask,
        VkPipelineStageFlags dst_stage_mask, VkDependencyFlags dependency_flags,
        uint32_t memory_barrier_count, const VkMemoryBarrier* memory_barriers,
        uint32_t buffer_memory_barrier_count, const VkBufferMemoryBarrier* buffer_memory_barriers,
        uint32_t image_memory_barrier_count,
        const VkImageMemoryBarrier* image_memory_barriers) const {
    (*pfn_vkCmdPipelineBarrier)(command_buffer, src_stage_mask, dst_stage_mask, dependency_flags,
                                memory_barrier_count, memory_barriers, buffer_memory_barrier_count,
                                buffer_memory_barriers, image_memory_barrier_count,
                                image_memory_barriers);
}

VkSubresourceLayout DeviceDispatch::getImageSubresourceLayout(const VkImage image,
                                                              const VkImageAspectFlags aspect_mask,
                                                              const uint32_t mip_level,
                                                              const uint32_t array_layer) const {
    const VkImageSubresource subresource = {
        .aspectMask = aspect_mask,
        .mipLevel = mip_level,
        .arrayLayer = array_layer,
    };

    VkSubresourceLayout layout;
    (*pfn_vkGetImageSubresourceLayout)(device_, image, &subresource, &layout);
    return layout;
}

}  // namespace goldfish::gvk
