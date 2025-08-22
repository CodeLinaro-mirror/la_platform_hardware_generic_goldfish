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

#include "goldfish/gvk/DeviceResources.h"

#include "absl/log/log.h"

#include "goldfish/gvk/DeviceDispatch.h"

namespace goldfish::gvk {

void DeviceResourceDeleter::operator()(VkBuffer buffer) const {
    mDeviceDispatch->destroyBuffer(buffer);
}
void DeviceResourceDeleter::operator()(VkCommandPool commandPool) const {
    mDeviceDispatch->destroyCommandPool(commandPool);
}
void DeviceResourceDeleter::operator()(VkDeviceMemory memory) const {
    mDeviceDispatch->freeMemory(memory);
}
void DeviceResourceDeleter::operator()(VkDescriptorPool descriptorPool) const {
    mDeviceDispatch->destroyDescriptorPool(descriptorPool);
}
void DeviceResourceDeleter::operator()(VkDescriptorSetLayout layout) const {
    mDeviceDispatch->destroyDescriptorSetLayout(layout);
}
void DeviceResourceDeleter::operator()(VkFramebuffer framebuffer) const {
    mDeviceDispatch->destroyFramebuffer(framebuffer);
}
void DeviceResourceDeleter::operator()(VkImage image) const {
    mDeviceDispatch->destroyImage(image);
}
void DeviceResourceDeleter::operator()(VkImageView imageView) const {
    mDeviceDispatch->destroyImageView(imageView);
}
void DeviceResourceDeleter::operator()(VkPipeline pipeline) const {
    mDeviceDispatch->destroyPipeline(pipeline);
}
void DeviceResourceDeleter::operator()(VkPipelineLayout pipelineLayout) const {
    mDeviceDispatch->destroyPipelineLayout(pipelineLayout);
}
void DeviceResourceDeleter::operator()(VkShaderModule shaderModule) const {
    mDeviceDispatch->destroyShaderModule(shaderModule);
}

void DeviceResourceDeleter::operator()(VkRenderPass renderPass) const {
    mDeviceDispatch->destroyRenderPass(renderPass);
}

CommandBufferEnderImpl::CommandBufferEnderImpl(PFN_vkEndCommandBuffer endCommandBuffer)
        : mEndCommandBuffer(endCommandBuffer) {}

void CommandBufferEnderImpl::operator()(VkCommandBuffer cmdbuf) const {
    const VkResult result = mEndCommandBuffer(cmdbuf);
    if (result != VK_SUCCESS) {
        LOG(FATAL) << "vkEndCommandBuffer failed with " << result;
    }
}

RenderPassEnderImpl::RenderPassEnderImpl(PFN_vkCmdEndRenderPass endRenderPass)
        : mEndRenderPass(endRenderPass) {}

void RenderPassEnderImpl::operator()(VkCommandBuffer cmdbuf) const {
    (*mEndRenderPass)(cmdbuf);
}

}  // namespace goldfish::gvk
