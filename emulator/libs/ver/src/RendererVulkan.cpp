/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "RendererVulkan.h"

#include <libyuv.h>

#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <functional>

#include "ScenesManager.h"
#include "aemu/base/files/PathUtils.h"
#include "aemu/base/logging/CLog.h"
#include "android/base/system.h"
#include "goldfish/gvk/system_meta_loader.h"

using namespace android::base;

#define E(...) derror(__VA_ARGS__)
#define W(...) dwarning(__VA_ARGS__)
#define D(...) dprint(__VA_ARGS__)

static constexpr int kRendererDefaultFramebufferWidth = 1024;
static constexpr int kRendererDefaultFramebufferHeight = 1024;

static uint64_t durationUsToMs(uint64_t startUs, uint64_t endUs) {
    assert(startUs <= endUs);
    const uint64_t durationUs = (endUs - startUs);
    return durationUs / 1000;
}

namespace android {
namespace ver {

class VulkanRendererContext : public RendererContext {
  public:
    VulkanRendererContext(bool valid) : mValid(valid) {}
    bool isValid() const override { return mValid; }

  private:
    bool mValid;
};

std::unique_ptr<RendererVulkan> RendererVulkan::create(
        const std::filesystem::path& vulkanBasePath) {
    dprint("virtualscene: Creating Vulkan renderer.");
    std::unique_ptr<RendererVulkan> renderer(new RendererVulkan(
            kRendererDefaultFramebufferWidth, kRendererDefaultFramebufferHeight, vulkanBasePath));
    if (!renderer->initialize()) {
        derror("virtualscene: could not create Vulkan renderer.");
        return nullptr;
    }
    return renderer;
}

RendererVulkan::RendererVulkan(int width, int height, const std::filesystem::path& vulkanBasePath)
        : mRenderWidth(width), mRenderHeight(height), mVulkanBasePath(vulkanBasePath) {}

bool RendererVulkan::initialize() {
    std::string customDir = mVulkanBasePath.string();
    auto metaLoader =
            goldfish::gvk::SystemMetaLoader::get(customDir.empty() ? nullptr : customDir.c_str());
    if (!metaLoader) {
        derror("VER: Cannot initialize Vulkan dispatcher via gvk::SystemMetaLoader.");
        return false;
    }

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "VER";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "VirtualEnvironmentRenderer";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceInfo = {};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;

    std::vector<const char*> extNames;
    std::vector<VkExtensionProperties> instanceExts =
            metaLoader->enumerateInstanceExtensionProperties(nullptr);
    for (const auto& ext : instanceExts) {
        if (strcmp(ext.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0) {
            instanceInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            extNames.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        } else if (strcmp(ext.extensionName,
                          VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME) == 0) {
            extNames.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        }
    }

    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extNames.size());
    instanceInfo.ppEnabledExtensionNames = extNames.data();

    mVkInstance = goldfish::gvk::InstanceDispatch::create(metaLoader, instanceInfo);
    if (!mVkInstance) {
        derror("gvk::InstanceDispatch::create failed.");
        return false;
    }

    uint32_t deviceCount = 0;
    mVkInstance->enumeratePhysicalDevices(&deviceCount, nullptr);
    if (deviceCount == 0) {
        derror("No physical Vulkan devices found.");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    mVkInstance->enumeratePhysicalDevices(&deviceCount, devices.data());
    mPhysicalDevice = devices[0];
    mMemoryProperties = mVkInstance->getPhysicalDeviceMemoryProperties(mPhysicalDevice);

    VkPhysicalDeviceProperties deviceProperties =
            mVkInstance->getPhysicalDeviceProperties(mPhysicalDevice);
    dinfo("VER: Using Vulkan device: %s, driver version: %u", deviceProperties.deviceName,
          deviceProperties.driverVersion);

    std::vector<VkExtensionProperties> availableDevExts =
            mVkInstance->enumerateDeviceExtensionProperties(mPhysicalDevice, nullptr);

    std::vector<const char*> enabledDevExts;
    for (const auto& ext : availableDevExts) {
        if (strcmp(ext.extensionName, "VK_KHR_portability_subset") == 0) {
            enabledDevExts.push_back("VK_KHR_portability_subset");
        }
    }

    auto devCreateRes = goldfish::gvk::DeviceDispatch::create(
            mVkInstance, mPhysicalDevice, VK_QUEUE_GRAPHICS_BIT, VK_NULL_HANDLE,
            static_cast<uint32_t>(enabledDevExts.size()), enabledDevExts.data());
    if (!devCreateRes.has_value()) {
        derror("gvk::DeviceDispatch::create failed.");
        return false;
    }

    mVkDevice = devCreateRes->dispatch;
    mQueueFamilyIndex = devCreateRes->queueLocations.graphics.familyIndex;
    mQueue = mVkDevice->getDeviceQueue(devCreateRes->queueLocations.graphics);

    mCommandPool = mVkDevice->createCommandPool(VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                                mQueueFamilyIndex);
    if (!mCommandPool) {
        derror("vkCreateCommandPool failed.");
        return false;
    }

    if (!mVkDevice->allocateCommandBuffers(mCommandPool.get(), VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1,
                                           &mCommandBuffer)) {
        derror("vkAllocateCommandBuffers failed.");
        return false;
    }

    mLoaderCommandPool = mVkDevice->createCommandPool(
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, mQueueFamilyIndex);
    if (!mLoaderCommandPool) {
        derror("vkCreateCommandPool loader failed.");
        return false;
    }

    if (!mVkDevice->allocateCommandBuffers(mLoaderCommandPool.get(),
                                           VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1,
                                           &mLoaderCommandBuffer)) {
        derror("vkAllocateCommandBuffers loader failed.");
        return false;
    }

    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    mSampler = mVkDevice->createSampler(samplerInfo);
    if (!mSampler) {
        derror("vkCreateSampler failed.");
        return false;
    }

    VkDescriptorSetLayoutBinding layoutBinding = {};
    layoutBinding.binding = 0;
    layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBinding.descriptorCount = 1;
    layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &layoutBinding;

    mDescriptorSetLayout = mVkDevice->createDescriptorSetLayout(layoutInfo);
    if (!mDescriptorSetLayout) {
        derror("vkCreateDescriptorSetLayout failed.");
        return false;
    }

    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 100;

    mDescriptorPool = mVkDevice->createDescriptorPool(
            VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, 100, 1, &poolSize);
    if (!mDescriptorPool) {
        derror("vkCreateDescriptorPool failed.");
        return false;
    }

    VkPushConstantRange meshPushConstant = {};
    meshPushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    meshPushConstant.offset = 0;
    meshPushConstant.size = 128;

    VkDescriptorSetLayout dsl = mDescriptorSetLayout.get();
    VkPipelineLayoutCreateInfo meshPipelineLayoutInfo = {};
    meshPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    meshPipelineLayoutInfo.setLayoutCount = 1;
    meshPipelineLayoutInfo.pSetLayouts = &dsl;
    meshPipelineLayoutInfo.pushConstantRangeCount = 1;
    meshPipelineLayoutInfo.pPushConstantRanges = &meshPushConstant;

    mMeshPipelineLayout = mVkDevice->createPipelineLayout(meshPipelineLayoutInfo);
    if (!mMeshPipelineLayout) {
        derror("vkCreatePipelineLayout mesh failed.");
        return false;
    }

    VkPushConstantRange screenPushConstant = {};
    screenPushConstant.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    screenPushConstant.offset = 0;
    screenPushConstant.size = 16;

    VkPipelineLayoutCreateInfo screenPipelineLayoutInfo = {};
    screenPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    screenPipelineLayoutInfo.setLayoutCount = 1;
    screenPipelineLayoutInfo.pSetLayouts = &dsl;
    screenPipelineLayoutInfo.pushConstantRangeCount = 1;
    screenPipelineLayoutInfo.pPushConstantRanges = &screenPushConstant;

    mScreenPipelineLayout = mVkDevice->createPipelineLayout(screenPipelineLayoutInfo);
    if (!mScreenPipelineLayout) {
        derror("vkCreatePipelineLayout screen failed.");
        return false;
    }

    // Create offscreen RenderPass (Color + Depth) and Screen RenderPass (Color only)
    {
        VkAttachmentDescription attachments[2] = {};
        attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        attachments[1].format = VK_FORMAT_D32_SFLOAT;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dep = {};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstAccessMask =
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo = {};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 2;
        rpInfo.pAttachments = attachments;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dep;

        mOffscreenRenderPass = mVkDevice->createRenderPass(rpInfo);
        if (!mOffscreenRenderPass) {
            derror("createRenderPass offscreen failed.");
            return false;
        }

        subpass.pDepthStencilAttachment = nullptr;
        rpInfo.attachmentCount = 1;
        mScreenRenderPass = mVkDevice->createRenderPass(rpInfo);
        if (!mScreenRenderPass) {
            derror("createRenderPass screen failed.");
            return false;
        }
    }

    if (!createVulkanImage(mRenderWidth, mRenderHeight, VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                   VK_IMAGE_USAGE_SAMPLED_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mOffscreenColorImage,
                           mOffscreenColorMemory, mOffscreenColorView)) {
        derror("createVulkanImage offscreen color failed.");
        return false;
    }

    if (!createVulkanImage(mRenderWidth, mRenderHeight, VK_FORMAT_D32_SFLOAT,
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mOffscreenDepthImage,
                           mOffscreenDepthMemory, mOffscreenDepthView)) {
        derror("createVulkanImage offscreen depth failed.");
        return false;
    }

    {
        VkImageView fbAttachments[2] = {mOffscreenColorView.get(), mOffscreenDepthView.get()};
        VkFramebufferCreateInfo fbInfo = {};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = mOffscreenRenderPass.get();
        fbInfo.attachmentCount = 2;
        fbInfo.pAttachments = fbAttachments;
        fbInfo.width = mRenderWidth;
        fbInfo.height = mRenderHeight;
        fbInfo.layers = 1;

        mOffscreenFramebuffer = mVkDevice->createFramebuffer(fbInfo);
        if (!mOffscreenFramebuffer) {
            derror("createFramebuffer offscreen failed.");
            return false;
        }
    }

    const VkDeviceSize stagingBufferSize = mRenderWidth * mRenderHeight * 4;
    if (!createVulkanBuffer(
                stagingBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                mStagingBuffer, mStagingMemory)) {
        derror("createVulkanBuffer staging failed.");
        return false;
    }

    mDefaultTexture = createEmptyTexture(1, 1);
    if (!mDefaultTexture.isValid()) {
        derror("Failed creating default texture.");
        return false;
    }

    mMaterialTextured = createMaterialTextured();
    if (!mMaterialTextured.isValid()) {
        derror("Failed creating material textured.");
        return false;
    }

    mLoaderThread = std::thread([this]() { loaderThreadLoop(); });
    mInitialized = true;
    return true;
}

RendererVulkan::~RendererVulkan() {
    if (mLoaderThread.joinable()) {
        enqueueLoaderCommand(LoaderCommand(LoaderCommandType::Shutdown, -1));
        mLoaderThread.join();
    }

    mMaterialTextured = Material();
    mDefaultTexture = Texture();

    if (mVkDevice) {
        mVkDevice->deviceWaitIdle();

        mMaterials.clear();
        mMeshes.clear();
        mTextures.clear();
        mTextureCache.clear();

        mMeshPipelineLayout.reset();
        mScreenPipelineLayout.reset();

        mOffscreenFramebuffer.reset();
        mOffscreenColorView.reset();
        mOffscreenColorImage.reset();
        mOffscreenColorMemory.reset();

        mOffscreenDepthView.reset();
        mOffscreenDepthImage.reset();
        mOffscreenDepthMemory.reset();

        mScreenRenderPass.reset();
        mOffscreenRenderPass.reset();

        mStagingBuffer.reset();
        mStagingMemory.reset();

        mSampler.reset();
        mDescriptorPool.reset();
        mDescriptorSetLayout.reset();

        mCommandPool.reset();
        mLoaderCommandPool.reset();

        mVkDevice.reset();
    }

    mVkInstance.reset();
}

void RendererVulkan::enqueueLoaderCommand(LoaderCommand command) {
    {
        std::lock_guard<std::mutex> lock(mLoaderQueueMutex);
        mLoaderQueue.push_back(std::move(command));
    }
    mLoaderQueueCv.notify_one();
}

void RendererVulkan::loaderThreadLoop() {
    while (true) {
        LoaderCommand cmd(LoaderCommandType::Shutdown, -1);
        {
            std::unique_lock<std::mutex> lock(mLoaderQueueMutex);
            mLoaderQueueCv.wait(lock, [this]() { return !mLoaderQueue.empty(); });
            cmd = std::move(mLoaderQueue.front());
            mLoaderQueue.pop_front();
        }
        if (cmd.mType == LoaderCommandType::Shutdown) {
            break;
        }
        if (cmd.mType == LoaderCommandType::LoadTexture) {
            onLoaderLoadTexture(Texture{cmd.mHandle});
        }
    }
}

uint32_t RendererVulkan::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    for (uint32_t i = 0; i < mMemoryProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (mMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

bool RendererVulkan::createVulkanBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                        VkMemoryPropertyFlags properties,
                                        goldfish::gvk::Buffer& buffer,
                                        goldfish::gvk::DeviceMemory& bufferMemory) {
    buffer = mVkDevice->createBuffer(size, usage);
    if (!buffer) {
        return false;
    }

    VkMemoryRequirements memRequirements = mVkDevice->getBufferMemoryRequirements(buffer.get());

    bufferMemory = mVkDevice->allocateMemory(
            memRequirements.size, findMemoryType(memRequirements.memoryTypeBits, properties));
    if (!bufferMemory) {
        buffer.reset();
        return false;
    }

    if (!mVkDevice->bindBufferMemory(buffer.get(), bufferMemory.get(), 0)) {
        buffer.reset();
        bufferMemory.reset();
        return false;
    }
    return true;
}

bool RendererVulkan::createVulkanImage(uint32_t width, uint32_t height, VkFormat format,
                                       VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                                       goldfish::gvk::Image& image,
                                       goldfish::gvk::DeviceMemory& imageMemory,
                                       goldfish::gvk::ImageView& imageView) {
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    image = mVkDevice->createImage(imageInfo);
    if (!image) {
        return false;
    }

    VkMemoryRequirements memRequirements = mVkDevice->getImageMemoryRequirements(image.get());

    imageMemory = mVkDevice->allocateMemory(
            memRequirements.size, findMemoryType(memRequirements.memoryTypeBits, properties));
    if (!imageMemory) {
        image.reset();
        return false;
    }

    if (!mVkDevice->bindImageMemory(image.get(), imageMemory.get(), 0)) {
        imageMemory.reset();
        image.reset();
        return false;
    }

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image.get();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask =
            (format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D16_UNORM)
                    ? VK_IMAGE_ASPECT_DEPTH_BIT
                    : VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    imageView = mVkDevice->createImageView(viewInfo);
    if (!imageView) {
        imageMemory.reset();
        image.reset();
        return false;
    }

    return true;
}

void RendererVulkan::submitCommandBuffer(VkCommandBuffer cmdBuffer) {
    AutoLock queueLock(mQueueLock);
    mVkDevice->queueSubmit(mQueue, cmdBuffer);
    mVkDevice->queueWaitIdle(mQueue);
}

void RendererVulkan::executeSingleTimeCommands(
        VkCommandBuffer cmdBuffer, const std::function<void(VkCommandBuffer)>& recordFunc) {
    mVkDevice->resetCommandBuffer(cmdBuffer, 0);
    {
        auto cmdEnder = mVkDevice->beginCommandBuffer(cmdBuffer, 0);
        recordFunc(cmdBuffer);
    }
    submitCommandBuffer(cmdBuffer);
}

void RendererVulkan::transitionImageLayout(VkCommandBuffer cmdBuffer, VkImage image,
                                           VkImageLayout oldLayout, VkImageLayout newLayout,
                                           VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
                                           VkPipelineStageFlags srcStageMask,
                                           VkPipelineStageFlags dstStageMask,
                                           VkImageAspectFlags aspectMask) {
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;

    mVkDevice->cmdPipelineBarrier(cmdBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr,
                                  1, &barrier);
}

std::optional<goldfish::gvk::ShaderModule> RendererVulkan::createShaderModule(
        const uint32_t* spirvCode, size_t codeSize) {
    auto mod = mVkDevice->createShaderModule(spirvCode, codeSize);
    if (!mod) {
        return std::nullopt;
    }
    return mod;
}

bool RendererVulkan::createGraphicsPipeline(VkShaderModule vertShader, VkShaderModule fragShader,
                                            VkPipelineLayout pipelineLayout,
                                            VkRenderPass renderPass, bool enableDepth,
                                            goldfish::gvk::Pipeline& pipeline) {
    VkPipelineShaderStageCreateInfo vertStageInfo = {};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertShader;
    vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo = {};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragShader;
    fragStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertStageInfo, fragStageInfo};

    VkVertexInputBindingDescription bindingDescription = {};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(VertexPositionUV);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[2] = {};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(VertexPositionUV, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(VertexPositionUV, uv);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = enableDepth ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = enableDepth ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    return mVkDevice->createGraphicsPipelines(1, &pipelineInfo, &pipeline) && pipeline;
}

float RendererVulkan::getAspectRatio() {
    return static_cast<float>(mRenderWidth) / mRenderHeight;
}

bool RendererVulkan::isTextureLoaded(Texture texture) {
    AutoLock lock(mResourceLock);
    auto it = mTextures.find(texture.id);
    if (it == mTextures.end()) return false;
    return it->second.mState == TextureState::Loaded;
}

void RendererVulkan::getTextureInfo(Texture texture, uint32_t* outWidth, uint32_t* outHeight) {
    AutoLock lock(mResourceLock);
    auto it = mTextures.find(texture.id);
    if (it != mTextures.end()) {
        *outWidth = it->second.mWidth;
        *outHeight = it->second.mHeight;
    } else {
        *outWidth = 0;
        *outHeight = 0;
    }
}

void RendererVulkan::releaseTexture(Texture texture) {
    if (!texture.isValid()) {
        return;
    }

    AutoLock lock(mResourceLock);
    if (texture.id == mDefaultTexture.id) {
        return;
    }

    auto it = mTextures.find(texture.id);
    if (it != mTextures.end()) {
        it->second.mRefCount--;
        if (it->second.mRefCount == 0) {
            if (!it->second.mFilename.empty()) {
                mTextureCache.erase(it->second.mFilename);
            }
            if (it->second.mDescriptorSet) {
                mVkDevice->freeDescriptorSets(mDescriptorPool.get(), 1, &it->second.mDescriptorSet);
            }
            mTextures.erase(it);
        }
    }
}

void RendererVulkan::releaseMaterial(Material material) {
    if (!material.isValid()) {
        return;
    }

    AutoLock lock(mResourceLock);
    if (!isStandardMaterial(material)) {
        releaseMaterialInternal(material);
    }
}

void RendererVulkan::releaseMaterialInternal(Material material) {
    auto it = mMaterials.find(material.id);
    if (it != mMaterials.end()) {
        mMaterials.erase(it);
    }
}

void RendererVulkan::releaseMesh(Mesh mesh) {
    if (!mesh.isValid()) {
        return;
    }

    AutoLock lock(mResourceLock);
    auto it = mMeshes.find(mesh.id);
    if (it != mMeshes.end()) {
        mMeshes.erase(it);
    }
}

Material RendererVulkan::createMaterialTextured() {
    // Return cached instance if it exists.
    if (mMaterialTextured.isValid()) {
        return mMaterialTextured;
    }

    AutoLock lock(mResourceLock);
    int id = mNextResourceId++;

    MaterialData data;
    data.pipelineLayout = mMeshPipelineLayout.get();
    auto vertOpt = createShaderModule(kTexturedVertSpirv, sizeof(kTexturedVertSpirv));
    auto fragOpt = createShaderModule(kTexturedFragSpirv, sizeof(kTexturedFragSpirv));
    if (!vertOpt || !fragOpt) {
        return Material{};
    }
    data.vertShader = std::move(*vertOpt);
    data.fragShader = std::move(*fragOpt);

    if (!createGraphicsPipeline(data.vertShader.get(), data.fragShader.get(), data.pipelineLayout,
                                mOffscreenRenderPass.get(),
                                /*enableDepth=*/true, data.pipeline)) {
        return Material{};
    }

    mMaterials.emplace(id, std::move(data));
    return Material{id};
}

Material RendererVulkan::createMaterialCheckerboard() {
    AutoLock lock(mResourceLock);
    int id = mNextResourceId++;

    MaterialData data;
    data.pipelineLayout = mMeshPipelineLayout.get();
    auto vertOpt = createShaderModule(kTexturedVertSpirv, sizeof(kTexturedVertSpirv));
    auto fragOpt = createShaderModule(kCheckerboardFragSpirv, sizeof(kCheckerboardFragSpirv));
    if (!vertOpt || !fragOpt) {
        return Material{};
    }
    data.vertShader = std::move(*vertOpt);
    data.fragShader = std::move(*fragOpt);

    if (!createGraphicsPipeline(data.vertShader.get(), data.fragShader.get(), data.pipelineLayout,
                                mOffscreenRenderPass.get(),
                                /*enableDepth=*/true, data.pipeline)) {
        return Material{};
    }

    mMaterials.emplace(id, std::move(data));
    return Material{id};
}

Material RendererVulkan::createMaterialScreenSpace(const char* frag) {
    AutoLock lock(mResourceLock);
    int id = mNextResourceId++;

    MaterialData data;
    data.isScreenSpace = true;
    data.pipelineLayout = mScreenPipelineLayout.get();
    auto vertOpt = createShaderModule(kScreenSpaceVertSpirv, sizeof(kScreenSpaceVertSpirv));
    std::optional<goldfish::gvk::ShaderModule> fragOpt;
    if (frag && std::string(frag).find("blit") != std::string::npos) {
        fragOpt = createShaderModule(kBlitFragSpirv, sizeof(kBlitFragSpirv));
    } else {
        fragOpt = createShaderModule(kFxaaFragSpirv, sizeof(kFxaaFragSpirv));
    }
    if (!vertOpt || !fragOpt) {
        return Material{};
    }
    data.vertShader = std::move(*vertOpt);
    data.fragShader = std::move(*fragOpt);

    if (!createGraphicsPipeline(data.vertShader.get(), data.fragShader.get(), data.pipelineLayout,
                                mScreenRenderPass.get(),
                                /*enableDepth=*/false, data.pipeline)) {
        return Material{};
    }

    mMaterials.emplace(id, std::move(data));
    return Material{id};
}

Mesh RendererVulkan::createMesh(const VertexPositionUV* vertices, size_t verticesSize,
                                const uint32_t* indices, size_t indicesSize) {
    if (!vertices || verticesSize == 0 || !indices || indicesSize == 0) {
        return Mesh{};
    }

    AutoLock lock(mResourceLock);
    int id = mNextResourceId++;

    MeshData meshData;
    meshData.mIndexCount = indicesSize;

    VkDeviceSize vertBufferSize = sizeof(VertexPositionUV) * verticesSize;
    if (!createVulkanBuffer(
                vertBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                meshData.mVertexBuffer, meshData.mVertexMemory)) {
        return Mesh{};
    }

    void* vMapped = mVkDevice->mapMemory(meshData.mVertexMemory.get(), vertBufferSize, 0);
    if (!vMapped) {
        return Mesh{};
    }
    memcpy(vMapped, vertices, vertBufferSize);
    mVkDevice->unmapMemory(meshData.mVertexMemory.get());

    VkDeviceSize indexBufferSize = sizeof(uint32_t) * indicesSize;
    if (!createVulkanBuffer(
                indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                meshData.mIndexBuffer, meshData.mIndexMemory)) {
        return Mesh{};
    }

    void* iMapped = mVkDevice->mapMemory(meshData.mIndexMemory.get(), indexBufferSize, 0);
    if (!iMapped) {
        return Mesh{};
    }
    memcpy(iMapped, indices, indexBufferSize);
    mVkDevice->unmapMemory(meshData.mIndexMemory.get());

    mMeshes.emplace(id, std::move(meshData));
    return Mesh{id};
}

Texture RendererVulkan::createEmptyTexture(uint32_t width, uint32_t height) {
    AutoLock lock(mResourceLock);
    int id = mNextResourceId++;

    TextureData data;
    data.mRefCount = 1;
    data.mState = TextureState::Loaded;
    data.mWidth = width;
    data.mHeight = height;

    if (!createVulkanImage(width, height, VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, data.mImage, data.mImageMemory,
                           data.mImageView)) {
        return Texture{-1};
    }

    executeSingleTimeCommands(mLoaderCommandBuffer, [this, &data](VkCommandBuffer cmd) {
        transitionImageLayout(cmd, data.mImage.get(), VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
                              VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    });

    VkDescriptorSetLayout dsl = mDescriptorSetLayout.get();
    if (!mVkDevice->allocateDescriptorSets(mDescriptorPool.get(), 1, &dsl, &data.mDescriptorSet)) {
        return Texture{-1};
    }

    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = data.mImageView.get();
    imageInfo.sampler = mSampler.get();

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = data.mDescriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;

    mVkDevice->updateDescriptorSets(1, &write, 0, nullptr);

    mTextures.emplace(id, std::move(data));
    return Texture{id};
}

bool RendererVulkan::isStandardMaterial(Material material) {
    if (material.id == mMaterialTextured.id) {
        return true;
    }

    return false;
}

Texture RendererVulkan::tryGetCachedTexture(const char* filename) {
    if (!filename) return Texture{-1};
    AutoLock lock(mResourceLock);
    auto it = mTextureCache.find(filename);
    if (it != mTextureCache.end()) {
        mTextures[it->second].mRefCount++;
        return Texture{it->second};
    }
    return Texture{-1};
}

void RendererVulkan::uploadTextureImage(TextureData& texData, const TextureUtils::Result& data) {
    texData.mImageView.reset();
    texData.mImage.reset();
    texData.mImageMemory.reset();

    texData.mWidth = data.mWidth;
    texData.mHeight = data.mHeight;

    if (!createVulkanImage(data.mWidth, data.mHeight, VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, texData.mImage,
                           texData.mImageMemory, texData.mImageView)) {
        return;
    }

    const uint8_t* pixelData = nullptr;
    size_t dataSize = 0;
    std::vector<uint8_t> convertedBuffer;

    if (data.mFormat == TextureUtils::Format::RGB24) {
        size_t srcStride = (static_cast<size_t>(data.mWidth) * 3 + 3) & ~3;
        dataSize = static_cast<size_t>(data.mWidth) * data.mHeight * 4;
        convertedBuffer.resize(dataSize);
        for (size_t y = 0; y < data.mHeight; ++y) {
            const uint8_t* srcRow = data.mBuffer.data() + y * srcStride;
            uint8_t* dstRow = convertedBuffer.data() + y * static_cast<size_t>(data.mWidth) * 4;
            for (size_t x = 0; x < data.mWidth; ++x) {
                dstRow[x * 4 + 0] = srcRow[x * 3 + 0];
                dstRow[x * 4 + 1] = srcRow[x * 3 + 1];
                dstRow[x * 4 + 2] = srcRow[x * 3 + 2];
                dstRow[x * 4 + 3] = 255;
            }
        }
        pixelData = convertedBuffer.data();
    } else {
        pixelData = data.mBuffer.data();
        dataSize = data.mBuffer.size();
    }

    if (pixelData && dataSize > 0) {
        goldfish::gvk::Buffer stagingBuf;
        goldfish::gvk::DeviceMemory stagingMem;
        if (createVulkanBuffer(
                    dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    stagingBuf, stagingMem)) {
            void* mapped = mVkDevice->mapMemory(stagingMem.get(), dataSize, 0);
            if (mapped) {
                memcpy(mapped, pixelData, dataSize);
                mVkDevice->unmapMemory(stagingMem.get());

                executeSingleTimeCommands(mLoaderCommandBuffer, [this, &texData, &data,
                                                                 &stagingBuf](VkCommandBuffer cmd) {
                    transitionImageLayout(
                            cmd, texData.mImage.get(), VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

                    VkBufferImageCopy region = {};
                    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    region.imageSubresource.layerCount = 1;
                    region.imageExtent = {data.mWidth, data.mHeight, 1};

                    mVkDevice->cmdCopyBufferToImage(cmd, stagingBuf.get(), texData.mImage.get(),
                                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                                    &region);

                    transitionImageLayout(
                            cmd, texData.mImage.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                });
            }
        }
    } else {
        executeSingleTimeCommands(mLoaderCommandBuffer, [this, &texData](VkCommandBuffer cmd) {
            transitionImageLayout(cmd, texData.mImage.get(), VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
                                  VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        });
    }

    if (!texData.mDescriptorSet) {
        VkDescriptorSetLayout dsl = mDescriptorSetLayout.get();
        if (!mVkDevice->allocateDescriptorSets(mDescriptorPool.get(), 1, &dsl,
                                               &texData.mDescriptorSet)) {
            return;
        }
    }

    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = texData.mImageView.get();
    imageInfo.sampler = mSampler.get();

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = texData.mDescriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;

    mVkDevice->updateDescriptorSets(1, &write, 0, nullptr);
}

Texture RendererVulkan::createTextureInternal(TextureState state, const char* filename,
                                              const TextureUtils::Result& data) {
    AutoLock lock(mResourceLock);
    int id = mNextResourceId++;

    TextureData texData;
    texData.mRefCount = 1;
    texData.mState = state;
    if (filename) texData.mFilename = filename;

    uploadTextureImage(texData, data);

    mTextures.emplace(id, std::move(texData));
    if (filename) mTextureCache[filename] = id;
    return Texture{id};
}

bool RendererVulkan::updateMesh(Mesh meshHandle, const VertexPositionUV* vertices,
                                size_t verticesSize) {
    if (!meshHandle.isValid() || !vertices || verticesSize == 0) {
        return false;
    }
    AutoLock lock(mResourceLock);
    auto it = mMeshes.find(meshHandle.id);
    if (it == mMeshes.end() || !it->second.mVertexMemory) {
        return false;
    }

    VkDeviceSize vertBufferSize = sizeof(VertexPositionUV) * verticesSize;
    void* mapped = mVkDevice->mapMemory(it->second.mVertexMemory.get(), vertBufferSize, 0);
    if (!mapped) {
        E("%s: Vulkan map memory failed", __FUNCTION__);
        return false;
    }

    memcpy(mapped, vertices, vertBufferSize);
    mVkDevice->unmapMemory(it->second.mVertexMemory.get());
    return true;
}

Texture RendererVulkan::loadTexture(const char* filename) {
    if (!filename) {
        derror("Invalid texture filename");
        return Texture{-1};
    }
    const uint64_t loadStartUs = System::Get()->GetHighResTimeUs();

    std::string path;
    if (!PathUtils::isAbsolute(filename)) {
        path = std::filesystem::absolute(filename).string();
    } else {
        path = filename;
    }

    Texture cached = tryGetCachedTexture(path.c_str());
    if (cached.isValid()) return cached;

    auto resOpt = TextureUtils::load(path.c_str());
    if (!resOpt.has_value()) return Texture{-1};

    const uint64_t loadEndUs = System::Get()->GetHighResTimeUs();

    Texture texture = createTextureInternal(TextureState::Loaded, path.c_str(), resOpt.value());

    const uint64_t importEndUs = System::Get()->GetHighResTimeUs();
    D("%s: Sync load texture %d in %" PRIu64 "ms, [read: %" PRIu64 "ms, import: %" PRIu64 "ms]",
      __FUNCTION__, texture.id, durationUsToMs(loadStartUs, importEndUs),
      durationUsToMs(loadStartUs, loadEndUs), durationUsToMs(loadEndUs, importEndUs));

    return texture;
}

Texture RendererVulkan::loadTextureAsync(const char* filename) {
    if (!filename) return Texture{-1};

    std::string path;
    if (!PathUtils::isAbsolute(filename)) {
        path = std::filesystem::absolute(filename).string();
    } else {
        path = filename;
    }

    Texture cached = tryGetCachedTexture(path.c_str());
    if (cached.isValid()) return cached;

    TextureUtils::Result placeholder = TextureUtils::createPlaceholder();
    Texture texture = createTextureInternal(TextureState::Placeholder, path.c_str(), placeholder);

    enqueueLoaderCommand(LoaderCommand(LoaderCommandType::LoadTexture, texture.id));
    return texture;
}

void RendererVulkan::onLoaderLoadTexture(Texture texture) {
    std::string filename;
    {
        AutoLock lock(mResourceLock);
        auto it = mTextures.find(texture.id);
        if (it == mTextures.end()) return;
        filename = it->second.mFilename;
    }

    if (!filename.empty()) {
        auto resOpt = TextureUtils::load(filename.c_str());
        if (resOpt.has_value()) {
            replaceTextureInternal(texture, resOpt.value());
        }
    }
}

bool RendererVulkan::replaceTextureInternal(Texture texture, const TextureUtils::Result& data) {
    AutoLock lock(mResourceLock);
    auto it = mTextures.find(texture.id);
    if (it == mTextures.end()) return false;

    uploadTextureImage(it->second, data);
    it->second.mState = TextureState::Loaded;
    return true;
}

Texture RendererVulkan::createTextureRGBA(const uint8_t* rgba, uint32_t width, uint32_t height) {
    if (!rgba || width == 0 || height == 0) {
        return Texture();
    }
    TextureUtils::Result result;
    result.mWidth = width;
    result.mHeight = height;
    result.mFormat = TextureUtils::Format::RGBA32;
    result.mBuffer.assign(rgba, rgba + (static_cast<size_t>(width) * height * 4));

    return createTextureInternal(TextureState::Loaded, nullptr, result);
}

Texture RendererVulkan::duplicateTexture(Texture texture) {
    AutoLock lock(mResourceLock);
    auto it = mTextures.find(texture.id);
    if (it != mTextures.end()) {
        it->second.mRefCount++;
        return texture;
    }
    return Texture{-1};
}

void RendererVulkan::drawRenderables(VkCommandBuffer cmd,
                                     const std::vector<RenderableObject>& renderables, float time) {
    struct PushConstants {
        glm::mat4 mvp;
        float time;
    };

    for (const auto& obj : renderables) {
        auto matIt = mMaterials.find(obj.renderable.material.id);
        auto meshIt = mMeshes.find(obj.renderable.mesh.id);

        if (matIt == mMaterials.end() || meshIt == mMeshes.end()) continue;

        const MaterialData& mat = matIt->second;
        const MeshData& mesh = meshIt->second;

        mVkDevice->cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mat.pipeline.get());

        auto texIt = mTextures.find(obj.renderable.texture.id);
        if (texIt != mTextures.end() && texIt->second.mDescriptorSet) {
            mVkDevice->cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             mat.pipelineLayout, 0, 1,
                                             &texIt->second.mDescriptorSet, 0, nullptr);
        } else {
            auto defaultTexIt = mTextures.find(mDefaultTexture.id);
            if (defaultTexIt != mTextures.end() && defaultTexIt->second.mDescriptorSet) {
                mVkDevice->cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                 mat.pipelineLayout, 0, 1,
                                                 &defaultTexIt->second.mDescriptorSet, 0, nullptr);
            }
        }

        PushConstants pushData;
        pushData.mvp = obj.modelViewProj;
        pushData.time = time;

        mVkDevice->cmdPushConstants(cmd, mat.pipelineLayout,
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                    sizeof(pushData), &pushData);

        VkBuffer vb = mesh.mVertexBuffer.get();
        VkDeviceSize offsets[] = {0};
        mVkDevice->cmdBindVertexBuffers(cmd, 0, 1, &vb, offsets);
        mVkDevice->cmdBindIndexBuffer(cmd, mesh.mIndexBuffer.get(), 0, VK_INDEX_TYPE_UINT32);

        mVkDevice->cmdDrawIndexed(cmd, mesh.mIndexCount, 1, 0, 0, 0);
    }
}

bool RendererVulkan::render(RendererView* view, const std::vector<RenderableObject>& renderables,
                            float time) {
    if (!view) return false;

    const int width = view->getWidthLocked();
    const int height = view->getHeightLocked();

    if (width <= 0 || height <= 0) return false;

    {
        AutoLock lock(mResourceLock);
        executeSingleTimeCommands(mCommandBuffer, [this, &renderables, time](VkCommandBuffer cmd) {
            VkClearValue clearValues[2] = {};
            clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            clearValues[1].depthStencil = {1.0f, 0};

            VkRect2D renderArea = {
                {0, 0},
                {static_cast<uint32_t>(mRenderWidth), static_cast<uint32_t>(mRenderHeight)}};

            {
                auto rpEnder = mVkDevice->cmdBeginRenderPass(
                        cmd, mOffscreenRenderPass.get(), mOffscreenFramebuffer.get(), renderArea, 2,
                        clearValues, VK_SUBPASS_CONTENTS_INLINE);

                VkViewport viewport = {
                    0.0f, 0.0f, static_cast<float>(mRenderWidth), static_cast<float>(mRenderHeight),
                    0.0f, 1.0f};
                VkRect2D scissor = {
                    {0, 0},
                    {static_cast<uint32_t>(mRenderWidth), static_cast<uint32_t>(mRenderHeight)}};

                mVkDevice->cmdSetViewport(cmd, 0, 1, &viewport);
                mVkDevice->cmdSetScissor(cmd, 0, 1, &scissor);

                drawRenderables(cmd, renderables, time);
            }

            VkImageMemoryBarrier transferBarrier = {};
            transferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            transferBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            transferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            transferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            transferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            transferBarrier.image = mOffscreenColorImage.get();
            transferBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            transferBarrier.subresourceRange.levelCount = 1;
            transferBarrier.subresourceRange.layerCount = 1;
            transferBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            transferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            mVkDevice->cmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                                          1, &transferBarrier);

            VkBufferImageCopy copyRegion = {};
            copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.imageSubresource.layerCount = 1;
            copyRegion.imageExtent = {static_cast<uint32_t>(mRenderWidth),
                                      static_cast<uint32_t>(mRenderHeight), 1};

            mVkDevice->cmdCopyImageToBuffer(cmd, mOffscreenColorImage.get(),
                                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                            mStagingBuffer.get(), 1, &copyRegion);
        });
    }

    // Read back RGBA pixels to view framebuffer
    void* mapped = mVkDevice->mapMemory(mStagingMemory.get(), mRenderWidth * mRenderHeight * 4, 0);
    if (!mapped) {
        derror("%s: Vulkan map memory failed for staging memory", __FUNCTION__);
        return false;
    }

    ImageScaler scaler(width, height, view->getFramebufferLocked().data());
    scaler.updateImage(mRenderWidth, mRenderHeight, static_cast<const uint8_t*>(mapped),
                       ImageScaler::ScaleMode::ScaleToFill);

    mVkDevice->unmapMemory(mStagingMemory.get());

    return true;
}

std::unique_ptr<RendererContext> RendererVulkan::makeCurrent() {
    return std::unique_ptr<RendererContext>(new VulkanRendererContext(mInitialized));
}

}  // namespace ver
}  // namespace android
