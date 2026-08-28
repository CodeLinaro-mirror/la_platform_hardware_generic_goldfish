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

#include "goldfish/gvk/device_resources.h"

#include "absl/log/log.h"

#include "goldfish/gvk/device_dispatch.h"

namespace goldfish::gvk {

void DeviceResourceDeleter::operator()(VkBuffer buffer) const {
    device_dispatch_->destroyBuffer(buffer);
}
void DeviceResourceDeleter::operator()(VkCommandPool command_pool) const {
    device_dispatch_->destroyCommandPool(command_pool);
}
void DeviceResourceDeleter::operator()(VkDeviceMemory memory) const {
    device_dispatch_->freeMemory(memory);
}
void DeviceResourceDeleter::operator()(VkDescriptorPool descriptor_pool) const {
    device_dispatch_->destroyDescriptorPool(descriptor_pool);
}
void DeviceResourceDeleter::operator()(VkDescriptorSetLayout layout) const {
    device_dispatch_->destroyDescriptorSetLayout(layout);
}
void DeviceResourceDeleter::operator()(VkFramebuffer framebuffer) const {
    device_dispatch_->destroyFramebuffer(framebuffer);
}
void DeviceResourceDeleter::operator()(VkImage image) const {
    device_dispatch_->destroyImage(image);
}
void DeviceResourceDeleter::operator()(VkImageView image_view) const {
    device_dispatch_->destroyImageView(image_view);
}
void DeviceResourceDeleter::operator()(VkPipeline pipeline) const {
    device_dispatch_->destroyPipeline(pipeline);
}
void DeviceResourceDeleter::operator()(VkPipelineLayout pipeline_layout) const {
    device_dispatch_->destroyPipelineLayout(pipeline_layout);
}
void DeviceResourceDeleter::operator()(VkShaderModule shader_module) const {
    device_dispatch_->destroyShaderModule(shader_module);
}

void DeviceResourceDeleter::operator()(VkRenderPass render_pass) const {
    device_dispatch_->destroyRenderPass(render_pass);
}

CommandBufferEnderImpl::CommandBufferEnderImpl(PFN_vkEndCommandBuffer end_command_buffer)
        : end_command_buffer_pfn_(end_command_buffer) {}

void CommandBufferEnderImpl::operator()(VkCommandBuffer cmdbuf) const {
    const VkResult result = end_command_buffer_pfn_(cmdbuf);
    if (result != VK_SUCCESS) {
        LOG(FATAL) << "vkEndCommandBuffer failed with " << result;
    }
}

RenderPassEnderImpl::RenderPassEnderImpl(PFN_vkCmdEndRenderPass end_render_pass)
        : end_render_pass_pfn_(end_render_pass) {}

void RenderPassEnderImpl::operator()(VkCommandBuffer cmdbuf) const {
    (*end_render_pass_pfn_)(cmdbuf);
}

}  // namespace goldfish::gvk
