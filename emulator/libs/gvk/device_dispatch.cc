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

#include "aemu/base/ArraySize.h"
#include "goldfish/debug.h"
#include "goldfish/gvk/util/build_device_queue_create_info.h"
#include "goldfish/gvk/util/init_pfn.h"
#include "goldfish/gvk/util/log_vk_result.h"

namespace goldfish::gvk {
using util::logVkResult;

namespace {
void stubForDestroyDevice(VkDevice, const VkAllocationCallbacks*) {
    LOG(ERROR) << "If you see this function called this means the Vulkan "
                  "implementation did not provide `vkDestroyDevice`.";
}
}  // namespace

DeviceDispatch::DeviceDispatch(InstanceDispatch::Ptr instanceDispatch, const VkDevice device,
                               const PFN_vkDestroyDevice destroyDevice, Private)
        : mInstanceDispatch(std::move(instanceDispatch))
        , mVkDevice(device)
        , mPFN_vkDestroyDevice(destroyDevice) {}

DeviceDispatch::~DeviceDispatch() {
    (*mPFN_vkDestroyDevice)(mVkDevice, nullptr);
}

DeviceDispatch::Ptr DeviceDispatch::create(const InstanceDispatch::Ptr& instanceDispatch,
                                           const VkPhysicalDevice physicalDevice,
                                           const VkDeviceCreateInfo& createInfo) {
    const VkDevice device = instanceDispatch->createDevice(physicalDevice, createInfo);
    if (!device) {
        return FAILURE(nullptr);
    }

    const auto getDeviceProcAddr = instanceDispatch->getDeviceProcAddr();
    const auto getPFN = [getDeviceProcAddr, device](const char* name) {
        return reinterpret_cast<void*>(getDeviceProcAddr(device, name));
    };

    PFN_vkDestroyDevice destroyDevice;
    if (!util::initPFN(destroyDevice, getPFN, "vkDestroyDevice", "DeviceDispatch::create")) {
        destroyDevice = &stubForDestroyDevice;
        LOG(ERROR) << "No way to destroy `vkDevice` because `vkDestroyDevice` is missing.";
    }

    auto deviceDispatch =
            std::make_shared<DeviceDispatch>(instanceDispatch, device, destroyDevice, Private());
    if (deviceDispatch->initPFNs(getPFN)) {
        return deviceDispatch;
    } else {
        return FAILURE(nullptr);
    }
}

std::optional<DeviceDispatch::CreateResult> DeviceDispatch::create(
        const InstanceDispatch::Ptr& instanceDispatch, const VkPhysicalDevice dev,
        const VkQueueFlags queueFlags0, const VkSurfaceKHR surface,
        const uint32_t enabledExtensionCount, const char* const* enabledExtensionNames,
        const VkPhysicalDeviceFeatures2* features2) {
    const VkQueueFlags queueFlags = queueFlags0 | (surface ? util::GVK_QUEUE_PRESENTATION_BIT : 0);

    std::vector<VkQueueFamilyProperties> queueFamilyProperties =
            instanceDispatch->getPhysicalDeviceQueueFamilyProperties(dev);

    auto [deviceQueueCreateInfos, deviceQueueLocations] = util::buildDeviceQueueCreateInfo(
            queueFamilyProperties.size(), queueFamilyProperties.data(), queueFlags);
    if (deviceQueueCreateInfos.empty()) {
        return FAILURE(std::nullopt);
    }

    const uint32_t maxQueuePrioCount = std::accumulate(
            deviceQueueCreateInfos.begin(), deviceQueueCreateInfos.end(), uint32_t(0),
            [](const uint32_t maxSoFar, const VkDeviceQueueCreateInfo& dqci) {
                return std::max(maxSoFar, dqci.queueCount);
            });

    static const float kGraphicsQueuePriorities8[] = {1.0f, 1.0f, 1.0f, 1.0f,
                                                      1.0f, 1.0f, 1.0f, 1.0f};

    std::vector<float> graphicsQueuePrioritiesVector;
    const float* graphicsQueuePriorities;
    if (maxQueuePrioCount <= ARRAY_SIZE(kGraphicsQueuePriorities8)) {
        graphicsQueuePriorities = kGraphicsQueuePriorities8;
    } else {
        graphicsQueuePrioritiesVector.resize(maxQueuePrioCount, kGraphicsQueuePriorities8[0]);
        graphicsQueuePriorities = graphicsQueuePrioritiesVector.data();
    }

    for (auto& qci : deviceQueueCreateInfos) {
        qci.pQueuePriorities = graphicsQueuePriorities;
    }

    const VkDeviceCreateInfo deviceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = features2,
        .flags = 0,
        .queueCreateInfoCount = uint32_t(deviceQueueCreateInfos.size()),
        .pQueueCreateInfos = deviceQueueCreateInfos.data(),
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = enabledExtensionCount,
        .ppEnabledExtensionNames = enabledExtensionNames,
        .pEnabledFeatures = nullptr,
    };

    Ptr dispatch = DeviceDispatch::create(instanceDispatch, dev, deviceCreateInfo);
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
#define INIT_1_PFN(F) util::initPFN(mPFN_##F, getPFN, #F, "DeviceDispatch::initPFNs") &&
    return GOLDFISH_GVK_DeviceDispatch_FUNC_LIST(INIT_1_PFN) true;
#undef INIT_1_PFN
}

/**************************************************************************************************/

VkQueue DeviceDispatch::getDeviceQueue(const uint32_t queueFamilyIndex,
                                       const uint32_t queueIndex) const {
    VkQueue queue = VK_NULL_HANDLE;
    (*mPFN_vkGetDeviceQueue)(mVkDevice, queueFamilyIndex, queueIndex, &queue);
    return queue;
}

DeviceMemory DeviceDispatch::allocateMemory(const size_t allocationSize,
                                            const uint32_t memoryTypeIndex) const {
    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = static_cast<VkDeviceSize>(allocationSize),
        .memoryTypeIndex = memoryTypeIndex,
    };

    VkDeviceMemory memory = VK_NULL_HANDLE;
    const VkResult result = (*mPFN_vkAllocateMemory)(mVkDevice, &allocInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        logVkResult("vkAllocateMemory", result);
        return {};
    }

    return DeviceMemory(memory, DeviceResourceDeleter(this));
}

void* DeviceDispatch::mapMemory(const VkDeviceMemory memory, const size_t size, const size_t offset,
                                const VkMemoryMapFlags flags) const {
    void* data = nullptr;
    const VkResult result = (*mPFN_vkMapMemory)(mVkDevice, memory, offset, size, flags, &data);
    if (result != VK_SUCCESS) {
        logVkResult("vkMapMemory", result);
        return nullptr;
    }

    return data;
}

void DeviceDispatch::unmapMemory(const VkDeviceMemory memory) const {
    (*mPFN_vkUnmapMemory)(mVkDevice, memory);
}

void DeviceDispatch::freeMemory(VkDeviceMemory memory) const {
    (*mPFN_vkFreeMemory)(mVkDevice, memory, nullptr);
}

Buffer DeviceDispatch::createBuffer(const size_t size, const VkBufferUsageFlags usage,
                                    const VkBufferCreateFlags flags,
                                    const uint32_t* queueFamilyIndices,
                                    const uint32_t queueFamilyIndicesSize) const {
    const VkBufferCreateInfo bufferCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = flags,
        .size = static_cast<VkDeviceSize>(size),
        .usage = usage,
        .sharingMode = (queueFamilyIndicesSize > 0) ? VK_SHARING_MODE_CONCURRENT
                                                    : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = queueFamilyIndicesSize,
        .pQueueFamilyIndices = queueFamilyIndices,
    };

    VkBuffer buffer = VK_NULL_HANDLE;
    VkResult result = (*mPFN_vkCreateBuffer)(mVkDevice, &bufferCreateInfo, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        logVkResult("vkCreateBuffer", result);
        return {};
    }

    return Buffer(buffer, DeviceResourceDeleter(this));
}

VkMemoryRequirements DeviceDispatch::getBufferMemoryRequirements(const VkBuffer buffer) const {
    VkMemoryRequirements memReqs;
    (*mPFN_vkGetBufferMemoryRequirements)(mVkDevice, buffer, &memReqs);
    return memReqs;
}

bool DeviceDispatch::bindBufferMemory(const VkBuffer buffer, const VkDeviceMemory memory,
                                      const size_t offset) const {
    const VkResult result = (*mPFN_vkBindBufferMemory)(mVkDevice, buffer, memory, offset);
    if (result != VK_SUCCESS) {
        logVkResult("vkBindBufferMemory", result);
        return false;
    }
    return true;
}

void DeviceDispatch::destroyBuffer(const VkBuffer buffer) const {
    (*mPFN_vkDestroyBuffer)(mVkDevice, buffer, nullptr);
}

CommandPool DeviceDispatch::createCommandPool(const VkCommandPoolCreateFlags flags,
                                              const uint32_t queueFamilyIndex) const {
    const VkCommandPoolCreateInfo cpci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = flags,
        .queueFamilyIndex = queueFamilyIndex,
    };

    VkCommandPool commandPool = VK_NULL_HANDLE;
    const VkResult result = (*mPFN_vkCreateCommandPool)(mVkDevice, &cpci, nullptr, &commandPool);
    if (result != VK_SUCCESS) {
        logVkResult("vkCreateCommandPool", result);
        return {};
    }

    return CommandPool(commandPool, DeviceResourceDeleter(this));
}

void DeviceDispatch::destroyCommandPool(VkCommandPool commandPool) const {
    (*mPFN_vkDestroyCommandPool)(mVkDevice, commandPool, nullptr);
}

bool DeviceDispatch::allocateCommandBuffers(const VkCommandPool commandPool,
                                            const VkCommandBufferLevel level,
                                            const uint32_t bufsSize, VkCommandBuffer* bufs) const {
    const VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = commandPool,
        .level = level,
        .commandBufferCount = bufsSize,
    };

    const VkResult result = (*mPFN_vkAllocateCommandBuffers)(mVkDevice, &cbai, bufs);
    if (result != VK_SUCCESS) {
        logVkResult("vkAllocateCommandBuffers", result);
        return false;
    }

    return true;
}

void DeviceDispatch::freeCommandBuffers(const VkCommandPool commandPool, const uint32_t bufsSize,
                                        const VkCommandBuffer* bufs) const {
    (*mPFN_vkFreeCommandBuffers)(mVkDevice, commandPool, bufsSize, bufs);
}

CommandBufferEnder DeviceDispatch::beginCommandBuffer(
        const VkCommandBuffer cmdbuf, const VkCommandBufferUsageFlags usageFlags,
        const bool occlusionQueryEnable, const VkQueryControlFlags queryFlags,
        const VkQueryPipelineStatisticFlags pipelineStatistics) const {
    const VkCommandBufferInheritanceInfo cbii = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        .pNext = nullptr,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
        .framebuffer = VK_NULL_HANDLE,
        .occlusionQueryEnable = (occlusionQueryEnable ? VK_TRUE : VK_FALSE),
        .queryFlags = queryFlags,
        .pipelineStatistics = pipelineStatistics,
    };

    const VkCommandBufferBeginInfo cbbi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = usageFlags,
        .pInheritanceInfo = &cbii,
    };

    const VkResult result = (*mPFN_vkBeginCommandBuffer)(cmdbuf, &cbbi);
    if (result != VK_SUCCESS) {
        logVkResult("vkBeginCommandBuffer", result);
        return {};
    }

    return CommandBufferEnder(cmdbuf, CommandBufferEnderImpl(mPFN_vkEndCommandBuffer));
}

void DeviceDispatch::cmdCopyBuffer(const VkCommandBuffer commandBuffer, const VkBuffer src,
                                   const VkBuffer dst, size_t size) const {
    const VkBufferCopy region = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size = static_cast<VkDeviceSize>(size),
    };

    (*mPFN_vkCmdCopyBuffer)(commandBuffer, src, dst, 1, &region);
}

bool DeviceDispatch::queueSubmit(VkQueue queue, const uint32_t submitCount,
                                 const VkSubmitInfo* submits, const VkFence fence) const {
    const VkResult result = (*mPFN_vkQueueSubmit)(queue, submitCount, submits, fence);
    if (result != VK_SUCCESS) {
        logVkResult("vkQueueSubmit", result);
        return false;
    }

    return true;
}

bool DeviceDispatch::queueSubmit(const VkQueue queue, const VkCommandBuffer commandBuffer,
                                 const VkFence fence) const {
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };
    return queueSubmit(queue, 1, &submitInfo, fence);
}

bool DeviceDispatch::queueWaitIdle(const VkQueue queue) const {
    const VkResult result = (*mPFN_vkQueueWaitIdle)(queue);
    if (result != VK_SUCCESS) {
        logVkResult("vkQueueWaitIdle", result);
        return false;
    }

    return true;
}

ShaderModule DeviceDispatch::createShaderModule(const void* code, const size_t codeSize) const {
    const VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = codeSize,
        .pCode = static_cast<const uint32_t*>(code),
    };

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    const VkResult result =
            (*mPFN_vkCreateShaderModule)(mVkDevice, &createInfo, nullptr, &shaderModule);
    if (result != VK_SUCCESS) {
        logVkResult("vkCreateShaderModule", result);
        return {};
    }

    return ShaderModule(shaderModule, DeviceResourceDeleter(this));
}

void DeviceDispatch::destroyShaderModule(const VkShaderModule shaderModule) const {
    (*mPFN_vkDestroyShaderModule)(mVkDevice, shaderModule, nullptr);
}

DescriptorSetLayout DeviceDispatch::createDescriptorSetLayout(
        const VkDescriptorSetLayoutCreateInfo& createInfo) const {
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    const VkResult result =
            (*mPFN_vkCreateDescriptorSetLayout)(mVkDevice, &createInfo, nullptr, &layout);
    if (result != VK_SUCCESS) {
        logVkResult("vkCreateDescriptorSetLayout", result);
        return {};
    }

    return DescriptorSetLayout(layout, DeviceResourceDeleter(this));
}

DescriptorSetLayout DeviceDispatch::createDescriptorSetLayout(
        const uint32_t bindingCount, const VkDescriptorSetLayoutBinding* bindings,
        const VkDescriptorSetLayoutCreateFlags flags) const {
    const VkDescriptorSetLayoutCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = flags,
        .bindingCount = bindingCount,
        .pBindings = bindings,
    };

    return createDescriptorSetLayout(createInfo);
}

void DeviceDispatch::destroyDescriptorSetLayout(const VkDescriptorSetLayout layout) const {
    (*mPFN_vkDestroyDescriptorSetLayout)(mVkDevice, layout, nullptr);
}

DescriptorPool DeviceDispatch::createDescriptorPool(VkDescriptorPoolCreateFlags, uint32_t maxSets,
                                                    uint32_t poolSizeCount,
                                                    const VkDescriptorPoolSize* poolSizes) const {
    const VkDescriptorPoolCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .maxSets = maxSets,
        .poolSizeCount = poolSizeCount,
        .pPoolSizes = poolSizes,
    };

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    const VkResult result =
            (*mPFN_vkCreateDescriptorPool)(mVkDevice, &createInfo, nullptr, &descriptorPool);
    if (result != VK_SUCCESS) {
        logVkResult("vkCreateDescriptorPool", result);
        return {};
    }

    return DescriptorPool(descriptorPool, DeviceResourceDeleter(this));
}

void DeviceDispatch::destroyDescriptorPool(VkDescriptorPool descriptorPool) const {
    (*mPFN_vkDestroyDescriptorPool)(mVkDevice, descriptorPool, nullptr);
}

bool DeviceDispatch::allocateDescriptorSets(const VkDescriptorPool descriptorPool,
                                            const uint32_t descriptorSetCount,
                                            const VkDescriptorSetLayout* setLayouts,
                                            VkDescriptorSet* descriptorSets) const {
    const VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = descriptorPool,
        .descriptorSetCount = descriptorSetCount,
        .pSetLayouts = setLayouts,
    };

    const VkResult result = (*mPFN_vkAllocateDescriptorSets)(mVkDevice, &allocInfo, descriptorSets);
    if (result != VK_SUCCESS) {
        logVkResult("vkAllocateDescriptorSets", result);
        return false;
    }

    return true;
}

void DeviceDispatch::updateDescriptorSets(const uint32_t descriptorWriteCount,
                                          const VkWriteDescriptorSet* descriptorWrites,
                                          const uint32_t descriptorCopyCount,
                                          const VkCopyDescriptorSet* descriptorCopies) const {
    (*mPFN_vkUpdateDescriptorSets)(mVkDevice, descriptorWriteCount, descriptorWrites,
                                   descriptorCopyCount, descriptorCopies);
}

bool DeviceDispatch::freeDescriptorSets(const VkDescriptorPool descriptorPool,
                                        const uint32_t descriptorSetCount,
                                        const VkDescriptorSet* descriptorSets) const {
    const VkResult result = (*mPFN_vkFreeDescriptorSets)(mVkDevice, descriptorPool,
                                                         descriptorSetCount, descriptorSets);
    if (result != VK_SUCCESS) {
        logVkResult("vkFreeDescriptorSets", result);
        return false;
    }

    return true;
}

Image DeviceDispatch::createImage(const VkImageCreateInfo& createInfo) const {
    VkImage image = VK_NULL_HANDLE;
    const VkResult result = (*mPFN_vkCreateImage)(mVkDevice, &createInfo, nullptr, &image);
    if (result != VK_SUCCESS) {
        logVkResult("vkCreateImage", result);
        return {};
    }

    return Image(image, DeviceResourceDeleter(this));
}

VkMemoryRequirements DeviceDispatch::getImageMemoryRequirements(const VkImage image) const {
    VkMemoryRequirements memReqs;
    (*mPFN_vkGetImageMemoryRequirements)(mVkDevice, image, &memReqs);
    return memReqs;
}

bool DeviceDispatch::bindImageMemory(const VkImage image, const VkDeviceMemory memory,
                                     const size_t offset) const {
    const VkResult result = (*mPFN_vkBindImageMemory)(mVkDevice, image, memory, offset);
    if (result != VK_SUCCESS) {
        logVkResult("vkBindImageMemory", result);
        return false;
    }

    return true;
}

void DeviceDispatch::destroyImage(const VkImage image) const {
    (*mPFN_vkDestroyImage)(mVkDevice, image, nullptr);
}

ImageView DeviceDispatch::createImageView(const VkImageViewCreateInfo& createInfo) const {
    VkImageView imageView = VK_NULL_HANDLE;
    const VkResult result = (*mPFN_vkCreateImageView)(mVkDevice, &createInfo, nullptr, &imageView);
    if (result != VK_SUCCESS) {
        logVkResult("vkCreateImageView", result);
        return {};
    }

    return ImageView(imageView, DeviceResourceDeleter(this));
}

ImageView DeviceDispatch::createImageView(const VkImage image, const VkImageViewType viewType,
                                          const VkFormat format,
                                          const VkImageAspectFlags aspectFlags) const {
    const VkImageViewCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = image,
        .viewType = viewType,
        .format = format,
        .components = {},  // zero-initialize to VK_COMPONENT_SWIZZLE_IDENTITY
        .subresourceRange =
                {
                    .aspectMask = aspectFlags,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
    };

    return createImageView(createInfo);
}

void DeviceDispatch::destroyImageView(const VkImageView imageView) const {
    (*mPFN_vkDestroyImageView)(mVkDevice, imageView, nullptr);
}

RenderPass DeviceDispatch::createRenderPass(const VkRenderPassCreateInfo& rpci) const {
    VkRenderPass renderPass = VK_NULL_HANDLE;
    const VkResult result = (*mPFN_vkCreateRenderPass)(mVkDevice, &rpci, nullptr, &renderPass);
    if (result != VK_SUCCESS) {
        logVkResult("vkCreateRenderPass", result);
        return {};
    }

    return RenderPass(renderPass, DeviceResourceDeleter(this));
}

void DeviceDispatch::destroyRenderPass(const VkRenderPass renderPass) const {
    (*mPFN_vkDestroyRenderPass)(mVkDevice, renderPass, nullptr);
}

Framebuffer DeviceDispatch::createFramebuffer(const VkFramebufferCreateInfo& createInfo) const {
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    const VkResult result =
            (*mPFN_vkCreateFramebuffer)(mVkDevice, &createInfo, nullptr, &framebuffer);
    if (result != VK_SUCCESS) {
        logVkResult("vkCreateFramebuffer", result);
        return {};
    }

    return Framebuffer(framebuffer, DeviceResourceDeleter(this));
}

Framebuffer DeviceDispatch::createFramebuffer(const VkRenderPass renderPass,
                                              const uint32_t attachmentCount,
                                              const VkImageView* attachments, const uint32_t width,
                                              const uint32_t height, const uint32_t layers) const {
    const VkFramebufferCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderPass = renderPass,
        .attachmentCount = attachmentCount,
        .pAttachments = attachments,
        .width = width,
        .height = height,
        .layers = layers,
    };

    return createFramebuffer(createInfo);
}

Framebuffer DeviceDispatch::createFramebuffer(const VkRenderPass renderPass,
                                              const VkImageView attachment, const uint32_t width,
                                              const uint32_t height, const uint32_t layers) const {
    return createFramebuffer(renderPass, 1, &attachment, width, height, layers);
}

void DeviceDispatch::destroyFramebuffer(const VkFramebuffer framebuffer) const {
    (*mPFN_vkDestroyFramebuffer)(mVkDevice, framebuffer, nullptr);
}

PipelineLayout DeviceDispatch::createPipelineLayout(
        const VkPipelineLayoutCreateInfo& createInfo) const {
    VkPipelineLayout layout = VK_NULL_HANDLE;
    const VkResult result =
            (*mPFN_vkCreatePipelineLayout)(mVkDevice, &createInfo, nullptr, &layout);
    if (result != VK_SUCCESS) {
        logVkResult("vkCreatePipelineLayout", result);
        return {};
    }

    return PipelineLayout(layout, DeviceResourceDeleter(this));
}

PipelineLayout DeviceDispatch::createPipelineLayout(
        const uint32_t setLayoutCount, const VkDescriptorSetLayout* setLayouts,
        const uint32_t pushConstantRangeCount,
        const VkPushConstantRange* pushConstantRanges) const {
    const VkPipelineLayoutCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = setLayoutCount,
        .pSetLayouts = setLayouts,
        .pushConstantRangeCount = pushConstantRangeCount,
        .pPushConstantRanges = pushConstantRanges,
    };

    return createPipelineLayout(createInfo);
}

void DeviceDispatch::destroyPipelineLayout(const VkPipelineLayout pipelineLayout) const {
    (*mPFN_vkDestroyPipelineLayout)(mVkDevice, pipelineLayout, nullptr);
}

bool DeviceDispatch::createGraphicsPipelinesImpl(uint32_t createInfoCount,
                                                 const VkGraphicsPipelineCreateInfo* createInfos,
                                                 VkPipeline* pipelinesTmp,
                                                 Pipeline* pipelines) const {
    const VkResult result = (*mPFN_vkCreateGraphicsPipelines)(
            mVkDevice, VK_NULL_HANDLE, createInfoCount, createInfos, nullptr, pipelinesTmp);
    if (result != VK_SUCCESS) {
        logVkResult("vkCreateGraphicsPipelines", result);
        return false;
    }

    for (; createInfoCount > 0; --createInfoCount, ++pipelinesTmp, ++pipelines) {
        *pipelines = Pipeline(*pipelinesTmp, DeviceResourceDeleter(this));
    }

    return true;
}

bool DeviceDispatch::createGraphicsPipelines(const uint32_t createInfoCount,
                                             const VkGraphicsPipelineCreateInfo* createInfos,
                                             Pipeline* pipelines) const {
    constexpr size_t kSmallSize = 8;
    if (createInfoCount <= kSmallSize) {
        VkPipeline pipelinesTmp[kSmallSize];
        return createGraphicsPipelinesImpl(createInfoCount, createInfos, pipelinesTmp, pipelines);
    } else {
        std::vector<VkPipeline> pipelinesTmp(createInfoCount);
        return createGraphicsPipelinesImpl(createInfoCount, createInfos, pipelinesTmp.data(),
                                           pipelines);
    }

    return true;
}

void DeviceDispatch::destroyPipeline(const VkPipeline pipeline) const {
    (*mPFN_vkDestroyPipeline)(mVkDevice, pipeline, nullptr);
}

RenderPassEnder DeviceDispatch::cmdBeginRenderPass(
        const VkCommandBuffer commandBuffer, const VkRenderPass renderPass,
        const VkFramebuffer framebuffer, const VkRect2D& renderArea, const uint32_t clearValueCount,
        const VkClearValue* clearValues, const VkSubpassContents subpassContents) const {
    const VkRenderPassBeginInfo renderPassBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext = nullptr,
        .renderPass = renderPass,
        .framebuffer = framebuffer,
        .renderArea = renderArea,
        .clearValueCount = clearValueCount,
        .pClearValues = clearValues,
    };

    (*mPFN_vkCmdBeginRenderPass)(commandBuffer, &renderPassBeginInfo, subpassContents);

    return RenderPassEnder(commandBuffer, RenderPassEnderImpl(mPFN_vkCmdEndRenderPass));
}

void DeviceDispatch::cmdBindPipeline(const VkCommandBuffer commandBuffer,
                                     const VkPipelineBindPoint bindPoint,
                                     const VkPipeline pipeline) const {
    (*mPFN_vkCmdBindPipeline)(commandBuffer, bindPoint, pipeline);
}

void DeviceDispatch::cmdBindDescriptorSets(const VkCommandBuffer commandBuffer,
                                           const VkPipelineBindPoint pipelineBindPoint,
                                           const VkPipelineLayout layout, const uint32_t firstSet,
                                           const uint32_t descriptorSetCount,
                                           const VkDescriptorSet* descriptorSets,
                                           const uint32_t dynamicOffsetCount,
                                           const uint32_t* dynamicOffsets) const {
    (*mPFN_vkCmdBindDescriptorSets)(commandBuffer, pipelineBindPoint, layout, firstSet,
                                    descriptorSetCount, descriptorSets, dynamicOffsetCount,
                                    dynamicOffsets);
}

void DeviceDispatch::cmdBindVertexBuffers(const VkCommandBuffer commandBuffer,
                                          const uint32_t firstBinding, const uint32_t bindingCount,
                                          const VkBuffer* buffers,
                                          const VkDeviceSize* offsets) const {
    (*mPFN_vkCmdBindVertexBuffers)(commandBuffer, firstBinding, bindingCount, buffers, offsets);
}

void DeviceDispatch::cmdBindIndexBuffer(const VkCommandBuffer commandBuffer, const VkBuffer buffer,
                                        const VkDeviceSize offset,
                                        const VkIndexType indexType) const {
    (*mPFN_vkCmdBindIndexBuffer)(commandBuffer, buffer, offset, indexType);
}

void DeviceDispatch::cmdDrawIndexed(const VkCommandBuffer commandBuffer, const uint32_t indexCount,
                                    const uint32_t instanceCount, const uint32_t firstIndex,
                                    const int32_t vertexOffset, uint32_t firstInstance) const {
    (*mPFN_vkCmdDrawIndexed)(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset,
                             firstInstance);
}

void DeviceDispatch::cmdBlitImage(const VkCommandBuffer commandBuffer, const VkImage srcImage,
                                  const VkImageLayout srcImageLayout, const VkImage dstImage,
                                  const VkImageLayout dstImageLayout, const uint32_t regionCount,
                                  const VkImageBlit* regions, const VkFilter filter) const {
    (*mPFN_vkCmdBlitImage)(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout,
                           regionCount, regions, filter);
}

void DeviceDispatch::cmdCopyImageToBuffer(const VkCommandBuffer commandBuffer,
                                          const VkImage srcImage,
                                          const VkImageLayout srcImageLayout,
                                          const VkBuffer dstBuffer, const uint32_t regionCount,
                                          const VkBufferImageCopy* regions) const {
    (*mPFN_vkCmdCopyImageToBuffer)(commandBuffer, srcImage, srcImageLayout, dstBuffer, regionCount,
                                   regions);
}

void DeviceDispatch::cmdPipelineBarrier(
        VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
        VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
        uint32_t memoryBarrierCount, const VkMemoryBarrier* memoryBarriers,
        uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* bufferMemoryBarriers,
        uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* imageMemoryBarriers) const {
    (*mPFN_vkCmdPipelineBarrier)(commandBuffer, srcStageMask, dstStageMask, dependencyFlags,
                                 memoryBarrierCount, memoryBarriers, bufferMemoryBarrierCount,
                                 bufferMemoryBarriers, imageMemoryBarrierCount,
                                 imageMemoryBarriers);
}

VkSubresourceLayout DeviceDispatch::getImageSubresourceLayout(const VkImage image,
                                                              const VkImageAspectFlags aspectMask,
                                                              const uint32_t mipLevel,
                                                              const uint32_t arrayLayer) const {
    const VkImageSubresource subresource = {
        .aspectMask = aspectMask,
        .mipLevel = mipLevel,
        .arrayLayer = arrayLayer,
    };

    VkSubresourceLayout layout;
    (*mPFN_vkGetImageSubresourceLayout)(mVkDevice, image, &subresource, &layout);
    return layout;
}

}  // namespace goldfish::gvk