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

#pragma once

#include <memory>
#include <optional>

#include "goldfish/gvk/DeviceQueueLocation.h"
#include "goldfish/gvk/DeviceResources.h"
#include "goldfish/gvk/GOLDFISH_GVK_POPULATE_MEMBER_PFN_VISITOR.h"
#include "goldfish/gvk/InstanceDispatch.h"
#include "goldfish/gvk/util/GetPFN.h"

#define GOLDFISH_GVK_DeviceDispatch_FUNC_LIST(VISITOR) \
    VISITOR(vkGetDeviceQueue)                          \
    VISITOR(vkAllocateMemory)                          \
    VISITOR(vkMapMemory)                               \
    VISITOR(vkUnmapMemory)                             \
    VISITOR(vkFreeMemory)                              \
    VISITOR(vkCreateBuffer)                            \
    VISITOR(vkGetBufferMemoryRequirements)             \
    VISITOR(vkBindBufferMemory)                        \
    VISITOR(vkDestroyBuffer)                           \
    VISITOR(vkCreateCommandPool)                       \
    VISITOR(vkDestroyCommandPool)                      \
    VISITOR(vkAllocateCommandBuffers)                  \
    VISITOR(vkFreeCommandBuffers)                      \
    VISITOR(vkBeginCommandBuffer)                      \
    VISITOR(vkCmdCopyBuffer)                           \
    VISITOR(vkCmdCopyImage)                            \
    VISITOR(vkEndCommandBuffer)                        \
    VISITOR(vkQueueSubmit)                             \
    VISITOR(vkQueueWaitIdle)                           \
    VISITOR(vkCreateShaderModule)                      \
    VISITOR(vkDestroyShaderModule)                     \
    VISITOR(vkCreateDescriptorSetLayout)               \
    VISITOR(vkDestroyDescriptorSetLayout)              \
    VISITOR(vkCreateDescriptorPool)                    \
    VISITOR(vkDestroyDescriptorPool)                   \
    VISITOR(vkAllocateDescriptorSets)                  \
    VISITOR(vkUpdateDescriptorSets)                    \
    VISITOR(vkFreeDescriptorSets)                      \
    VISITOR(vkCreateImage)                             \
    VISITOR(vkGetImageMemoryRequirements)              \
    VISITOR(vkBindImageMemory)                         \
    VISITOR(vkDestroyImage)                            \
    VISITOR(vkCreateImageView)                         \
    VISITOR(vkDestroyImageView)                        \
    VISITOR(vkCreateRenderPass)                        \
    VISITOR(vkDestroyRenderPass)                       \
    VISITOR(vkCreateFramebuffer)                       \
    VISITOR(vkDestroyFramebuffer)                      \
    VISITOR(vkCreatePipelineLayout)                    \
    VISITOR(vkDestroyPipelineLayout)                   \
    VISITOR(vkCreateGraphicsPipelines)                 \
    VISITOR(vkDestroyPipeline)                         \
    VISITOR(vkCmdBeginRenderPass)                      \
    VISITOR(vkCmdEndRenderPass)                        \
    VISITOR(vkCmdBindPipeline)                         \
    VISITOR(vkCmdBindDescriptorSets)                   \
    VISITOR(vkCmdBindVertexBuffers)                    \
    VISITOR(vkCmdBindIndexBuffer)                      \
    VISITOR(vkCmdDrawIndexed)                          \
    VISITOR(vkCmdBlitImage)                            \
    VISITOR(vkCmdCopyImageToBuffer)                    \
    VISITOR(vkCmdPipelineBarrier)                      \
    VISITOR(vkGetImageSubresourceLayout)

namespace goldfish::gvk {

class DeviceDispatch {
  public:
    using Ptr = std::shared_ptr<const DeviceDispatch>;

    struct CreateResult {
        Ptr dispatch;
        DeviceQueueLocations queueLocations;
    };

    static std::optional<CreateResult> create(const InstanceDispatch::Ptr&, VkPhysicalDevice,
                                              VkQueueFlags, VkSurfaceKHR,
                                              uint32_t enabledExtensionCount = 0,
                                              const char* const* enabledExtensionNames = nullptr,
                                              const VkPhysicalDeviceFeatures2* features2 = nullptr);

    VkQueue getDeviceQueue(uint32_t queueFamilyIndex, uint32_t queueIndex) const;

    VkQueue getDeviceQueue(const DeviceQueueLocation dql) const {
        return getDeviceQueue(dql.familyIndex, dql.queueIndex);
    }

    DeviceMemory allocateMemory(size_t allocationSize, uint32_t memoryTypeIndex) const;
    void* mapMemory(VkDeviceMemory, size_t size, size_t offset = 0, VkMemoryMapFlags = 0) const;
    void unmapMemory(VkDeviceMemory) const;
    void freeMemory(VkDeviceMemory) const;

    Buffer createBuffer(size_t size, VkBufferUsageFlags, VkBufferCreateFlags = 0,
                        const uint32_t* queueFamilyIndices = nullptr,
                        uint32_t queueFamilyIndicesSize = 0) const;
    VkMemoryRequirements getBufferMemoryRequirements(VkBuffer) const;
    bool bindBufferMemory(VkBuffer, VkDeviceMemory, size_t offset) const;
    void destroyBuffer(VkBuffer) const;

    CommandPool createCommandPool(VkCommandPoolCreateFlags, uint32_t queueFamilyIndex) const;
    void destroyCommandPool(VkCommandPool) const;

    bool allocateCommandBuffers(VkCommandPool, VkCommandBufferLevel, uint32_t bufsSize,
                                VkCommandBuffer* bufs) const;
    void freeCommandBuffers(VkCommandPool, uint32_t bufsSize, const VkCommandBuffer* bufs) const;

    CommandBufferEnder beginCommandBuffer(VkCommandBuffer, VkCommandBufferUsageFlags,
                                          bool occlusionQueryEnable = false,
                                          VkQueryControlFlags = 0,
                                          VkQueryPipelineStatisticFlags = 0) const;

    void cmdCopyBuffer(VkCommandBuffer, VkBuffer src, VkBuffer dst, size_t size) const;

    bool queueSubmit(VkQueue, uint32_t submitCount, const VkSubmitInfo* submits,
                     VkFence = VK_NULL_HANDLE) const;
    bool queueSubmit(VkQueue, VkCommandBuffer, VkFence = VK_NULL_HANDLE) const;
    bool queueWaitIdle(VkQueue) const;

    ShaderModule createShaderModule(const void* code, size_t codeSize) const;
    void destroyShaderModule(VkShaderModule) const;

    DescriptorSetLayout createDescriptorSetLayout(const VkDescriptorSetLayoutCreateInfo&) const;
    DescriptorSetLayout createDescriptorSetLayout(uint32_t bindingCount,
                                                  const VkDescriptorSetLayoutBinding* bindings,
                                                  VkDescriptorSetLayoutCreateFlags = 0) const;
    void destroyDescriptorSetLayout(VkDescriptorSetLayout) const;

    DescriptorPool createDescriptorPool(VkDescriptorPoolCreateFlags, uint32_t maxSets,
                                        uint32_t poolSizeCount,
                                        const VkDescriptorPoolSize* poolSizes) const;
    void destroyDescriptorPool(VkDescriptorPool) const;

    bool allocateDescriptorSets(VkDescriptorPool, uint32_t descriptorSetCount,
                                const VkDescriptorSetLayout* setLayouts,
                                VkDescriptorSet* descriptorSets) const;
    void updateDescriptorSets(uint32_t descriptorWriteCount,
                              const VkWriteDescriptorSet* descriptorWrites,
                              uint32_t descriptorCopyCount,
                              const VkCopyDescriptorSet* descriptorCopies) const;
    bool freeDescriptorSets(VkDescriptorPool, uint32_t descriptorSetCount,
                            const VkDescriptorSet* descriptorSets) const;

    Image createImage(const VkImageCreateInfo&) const;
    VkMemoryRequirements getImageMemoryRequirements(VkImage) const;
    bool bindImageMemory(VkImage, VkDeviceMemory, size_t offset) const;
    void destroyImage(VkImage) const;

    ImageView createImageView(const VkImageViewCreateInfo&) const;
    ImageView createImageView(VkImage, VkImageViewType, VkFormat, VkImageAspectFlags) const;
    void destroyImageView(VkImageView) const;

    RenderPass createRenderPass(const VkRenderPassCreateInfo&) const;
    void destroyRenderPass(VkRenderPass) const;

    Framebuffer createFramebuffer(const VkFramebufferCreateInfo&) const;
    Framebuffer createFramebuffer(VkRenderPass, uint32_t attachmentCount,
                                  const VkImageView* attachments, uint32_t width, uint32_t height,
                                  uint32_t layers = 1) const;
    Framebuffer createFramebuffer(VkRenderPass, VkImageView, uint32_t width, uint32_t height,
                                  uint32_t layers = 1) const;
    void destroyFramebuffer(VkFramebuffer) const;

    PipelineLayout createPipelineLayout(const VkPipelineLayoutCreateInfo&) const;
    PipelineLayout createPipelineLayout(
            uint32_t setLayoutCount, const VkDescriptorSetLayout* setLayouts,
            uint32_t pushConstantRangeCount = 0,
            const VkPushConstantRange* pushConstantRanges = nullptr) const;
    void destroyPipelineLayout(VkPipelineLayout) const;

    bool createGraphicsPipelines(uint32_t createInfoCount,
                                 const VkGraphicsPipelineCreateInfo* createInfos,
                                 Pipeline* pipelines) const;
    void destroyPipeline(VkPipeline pipeline) const;

    RenderPassEnder cmdBeginRenderPass(VkCommandBuffer, VkRenderPass, VkFramebuffer,
                                       const VkRect2D& renderArea, uint32_t clearValueCount,
                                       const VkClearValue* clearValues, VkSubpassContents) const;

    void cmdBindPipeline(VkCommandBuffer, VkPipelineBindPoint, VkPipeline) const;
    void cmdBindDescriptorSets(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout,
                               uint32_t firstSet, uint32_t descriptorSetCount,
                               const VkDescriptorSet* descriptorSets,
                               uint32_t dynamicOffsetCount = 0,
                               const uint32_t* dynamicOffsets = nullptr) const;
    void cmdBindVertexBuffers(VkCommandBuffer, uint32_t firstBinding, uint32_t bindingCount,
                              const VkBuffer* buffers, const VkDeviceSize* offsets) const;
    void cmdBindIndexBuffer(VkCommandBuffer, VkBuffer, VkDeviceSize offset,
                            VkIndexType indexType) const;
    void cmdDrawIndexed(VkCommandBuffer, uint32_t indexCount, uint32_t instanceCount,
                        uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) const;
    void cmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout,
                      VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
                      const VkImageBlit* regions, VkFilter filter) const;
    void cmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage,
                              VkImageLayout srcImageLayout, VkBuffer dstBuffer,
                              uint32_t regionCount, const VkBufferImageCopy* regions) const;
    void cmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
                            VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
                            uint32_t memoryBarrierCount, const VkMemoryBarrier* memoryBarriers,
                            uint32_t bufferMemoryBarrierCount,
                            const VkBufferMemoryBarrier* bufferMemoryBarriers,
                            uint32_t imageMemoryBarrierCount,
                            const VkImageMemoryBarrier* imageMemoryBarriers) const;

    VkSubresourceLayout getImageSubresourceLayout(VkImage image, VkImageAspectFlags aspectMask,
                                                  uint32_t mipLevel, uint32_t arrayLayer) const;

  private:
    struct Private {};

    static Ptr create(const InstanceDispatch::Ptr&, VkPhysicalDevice, const VkDeviceCreateInfo&);
    bool initPFNs(const util::GetPFN&);

    const InstanceDispatch::Ptr mInstanceDispatch;
    const VkDevice mVkDevice;
    const PFN_vkDestroyDevice mPFN_vkDestroyDevice;

    bool createGraphicsPipelinesImpl(uint32_t createInfoCount,
                                     const VkGraphicsPipelineCreateInfo* createInfos,
                                     VkPipeline* pipelinesTmp, Pipeline* pipelines) const;

    GOLDFISH_GVK_DeviceDispatch_FUNC_LIST(GOLDFISH_GVK_POPULATE_MEMBER_PFN_VISITOR);

  public:
    DeviceDispatch(InstanceDispatch::Ptr, VkDevice, PFN_vkDestroyDevice, Private);
    ~DeviceDispatch();

    DeviceDispatch(const DeviceDispatch&) = delete;
    DeviceDispatch(DeviceDispatch&&) = delete;
    DeviceDispatch& operator=(const DeviceDispatch&) = delete;
    DeviceDispatch& operator=(DeviceDispatch&&) = delete;
};

}  // namespace goldfish::gvk
