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

#include <vulkan/vulkan.h>

#include "goldfish/base/unique_handle.h"

namespace goldfish::gvk {

class DeviceDispatch;

struct DeviceResourceDeleter {
    struct Empty {};

    explicit DeviceResourceDeleter(const DeviceDispatch* dd) : device_dispatch_(dd) {}
    DeviceResourceDeleter(Empty) : device_dispatch_(nullptr) {}

    void operator()(VkBuffer) const;
    void operator()(VkCommandPool) const;
    void operator()(VkDeviceMemory) const;
    void operator()(VkDescriptorPool) const;
    void operator()(VkDescriptorSetLayout) const;
    void operator()(VkFramebuffer) const;
    void operator()(VkImage) const;
    void operator()(VkImageView) const;
    void operator()(VkPipeline) const;
    void operator()(VkPipelineLayout) const;
    void operator()(VkRenderPass) const;
    void operator()(VkShaderModule) const;

  private:
    const DeviceDispatch* device_dispatch_;
};

struct CommandBufferEnderImpl {
    struct Empty {};

    explicit CommandBufferEnderImpl(PFN_vkEndCommandBuffer endCommandBuffer);
    CommandBufferEnderImpl(Empty) {}

    void operator()(VkCommandBuffer) const;

  private:
    PFN_vkEndCommandBuffer end_command_buffer_pfn_ = nullptr;
};

struct RenderPassEnderImpl {
    struct Empty {};

    explicit RenderPassEnderImpl(PFN_vkCmdEndRenderPass endRenderPass);
    RenderPassEnderImpl(Empty) {}

    void operator()(VkCommandBuffer) const;

  private:
    PFN_vkCmdEndRenderPass end_render_pass_pfn_ = nullptr;
};

using Buffer = goldfish::base::UniqueHandle<VkBuffer, VK_NULL_HANDLE, DeviceResourceDeleter>;
using CommandPool =
        goldfish::base::UniqueHandle<VkCommandPool, VK_NULL_HANDLE, DeviceResourceDeleter>;
using CommandBufferEnder =
        goldfish::base::UniqueHandle<VkCommandBuffer, VK_NULL_HANDLE, CommandBufferEnderImpl>;
using DeviceMemory =
        goldfish::base::UniqueHandle<VkDeviceMemory, VK_NULL_HANDLE, DeviceResourceDeleter>;
using DescriptorPool =
        goldfish::base::UniqueHandle<VkDescriptorPool, VK_NULL_HANDLE, DeviceResourceDeleter>;
using DescriptorSetLayout =
        goldfish::base::UniqueHandle<VkDescriptorSetLayout, VK_NULL_HANDLE, DeviceResourceDeleter>;
using Framebuffer =
        goldfish::base::UniqueHandle<VkFramebuffer, VK_NULL_HANDLE, DeviceResourceDeleter>;
using Image = goldfish::base::UniqueHandle<VkImage, VK_NULL_HANDLE, DeviceResourceDeleter>;
using ImageView = goldfish::base::UniqueHandle<VkImageView, VK_NULL_HANDLE, DeviceResourceDeleter>;
using Pipeline = goldfish::base::UniqueHandle<VkPipeline, VK_NULL_HANDLE, DeviceResourceDeleter>;
using PipelineLayout =
        goldfish::base::UniqueHandle<VkPipelineLayout, VK_NULL_HANDLE, DeviceResourceDeleter>;
using RenderPass =
        goldfish::base::UniqueHandle<VkRenderPass, VK_NULL_HANDLE, DeviceResourceDeleter>;
using RenderPassEnder =
        goldfish::base::UniqueHandle<VkCommandBuffer, VK_NULL_HANDLE, RenderPassEnderImpl>;
using ShaderModule =
        goldfish::base::UniqueHandle<VkShaderModule, VK_NULL_HANDLE, DeviceResourceDeleter>;

}  // namespace goldfish::gvk
