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

#pragma once

#include <vulkan/vulkan.h>

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Renderer.h"
#include "TextureUtils.h"
#include "VulkanShaders.h"
#include "goldfish/gvk/device_dispatch.h"
#include "goldfish/gvk/device_resources.h"
#include "goldfish/gvk/instance_dispatch.h"

namespace android {
namespace ver {

class RendererVulkan : public Renderer {
    RendererVulkan(const RendererVulkan&) = delete;
    RendererVulkan& operator=(const RendererVulkan&) = delete;

  public:
    RendererVulkan(int width, int height, const std::filesystem::path& vulkanBasePath = {});
    ~RendererVulkan() override;

    static std::unique_ptr<RendererVulkan> create(const std::filesystem::path& vulkanBasePath);

    bool initialize();

    // Renderer public API.
    float getAspectRatio() override;
    bool isTextureLoaded(Texture texture) override;
    bool getTextureInfo(Texture texture, uint32_t* outWidth, uint32_t* outHeight,
                        uint64_t* outGeneration) override;

    void releaseTexture(Texture texture) override;
    void releaseMaterial(Material material) override;
    void releaseMesh(Mesh mesh) override;

    Material createMaterialCheckerboard() override;
    Material createMaterialTextured() override;
    Material createMaterialScreenSpace(const char* frag) override;

    Mesh createMesh(const VertexPositionUV* vertices, size_t verticesSize, const uint32_t* indices,
                    size_t indicesSize) override;

    bool updateMesh(Mesh mesh, const VertexPositionUV* vertices, size_t verticesSize) override;

    Texture loadTexture(const char* filename) override;
    Texture loadTextureAsync(const char* filename) override;
    Texture createTextureRGBA(const uint8_t* rgba, uint32_t width, uint32_t height) override;
    Texture duplicateTexture(Texture texture) override;

    bool render(RendererView* view, const std::vector<RenderableObject>& renderables,
                float time) override;

    std::unique_ptr<RendererContext> makeCurrent() override;

  private:
    struct MaterialData {
        goldfish::gvk::Pipeline pipeline;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        goldfish::gvk::ShaderModule vertShader;
        goldfish::gvk::ShaderModule fragShader;
        bool isScreenSpace = false;
    };

    struct MeshData {
        VertexInfo mVertexInfo;
        goldfish::gvk::Buffer mVertexBuffer;
        goldfish::gvk::DeviceMemory mVertexMemory;
        goldfish::gvk::Buffer mIndexBuffer;
        goldfish::gvk::DeviceMemory mIndexMemory;
        size_t mIndexCount = 0;
    };

    enum class TextureState { Placeholder, Loaded };

    // Identifies the version of a file on disk, used to detect that a cached
    // texture is stale because its source file has been modified.
    struct FileStamp {
        bool valid = false;
        std::filesystem::file_time_type lastWriteTime;
        std::uintmax_t size = 0;

        bool operator==(const FileStamp& rhs) const {
            return valid == rhs.valid && lastWriteTime == rhs.lastWriteTime && size == rhs.size;
        }
        bool operator!=(const FileStamp& rhs) const { return !(*this == rhs); }
    };

    struct TextureData {
        size_t mRefCount = 0;
        TextureState mState = TextureState::Loaded;
        goldfish::gvk::Image mImage;
        goldfish::gvk::DeviceMemory mImageMemory;
        goldfish::gvk::ImageView mImageView;
        VkDescriptorSet mDescriptorSet = VK_NULL_HANDLE;
        std::string mFilename;
        FileStamp mFileStamp;
        uint32_t mWidth = 0;
        uint32_t mHeight = 0;
        uint64_t mGeneration = 0;
    };

    enum class LoaderCommandType { Shutdown, LoadTexture };

    struct LoaderCommand {
        LoaderCommandType mType;
        int mHandle = -1;

        LoaderCommand(LoaderCommandType type, int handle) : mType(type), mHandle(handle) {}
    };

    void enqueueLoaderCommand(LoaderCommand command);
    void loaderThreadLoop();

    void releaseMaterialInternal(Material material);
    bool isStandardMaterial(Material material);
    static FileStamp getFileStamp(const std::string& filename);
    Texture tryGetCachedTexture(const std::string& filename, const FileStamp& fileStamp);
    Texture createEmptyTexture(uint32_t width, uint32_t height);
    Texture createTextureInternal(TextureState state, const char* filename,
                                  const FileStamp& fileStamp, const TextureUtils::Result& data);
    void onLoaderLoadTexture(Texture texture);
    bool replaceTextureInternal(Texture texture, const TextureUtils::Result& data);

    bool uploadTextureImage(TextureData& texData, const TextureUtils::Result& data);

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    bool createVulkanBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags properties, goldfish::gvk::Buffer& buffer,
                            goldfish::gvk::DeviceMemory& bufferMemory);

    bool createVulkanImage(uint32_t width, uint32_t height, VkFormat format,
                           VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                           goldfish::gvk::Image& image, goldfish::gvk::DeviceMemory& imageMemory,
                           goldfish::gvk::ImageView& imageView);

    void submitCommandBuffer(VkCommandBuffer cmdBuffer);

    void executeSingleTimeCommands(VkCommandBuffer cmdBuffer,
                                   const std::function<void(VkCommandBuffer)>& recordFunc);

    void transitionImageLayout(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout,
                               VkImageLayout newLayout, VkAccessFlags srcAccessMask,
                               VkAccessFlags dstAccessMask, VkPipelineStageFlags srcStageMask,
                               VkPipelineStageFlags dstStageMask,
                               VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);

    void drawRenderables(VkCommandBuffer cmd, const std::vector<RenderableObject>& renderables,
                         float time);

    std::optional<goldfish::gvk::ShaderModule> createShaderModule(const uint32_t* spirvCode,
                                                                  size_t codeSize);

    bool createGraphicsPipeline(VkShaderModule vertShader, VkShaderModule fragShader,
                                VkPipelineLayout pipelineLayout, VkRenderPass renderPass,
                                bool enableDepth, goldfish::gvk::Pipeline& pipeline);

    const int mRenderWidth;
    const int mRenderHeight;
    const std::filesystem::path mVulkanBasePath;

    goldfish::gvk::InstanceDispatch::Ptr mVkInstance;
    goldfish::gvk::DeviceDispatch::Ptr mVkDevice;

    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mMemoryProperties = {};
    VkQueue mQueue = VK_NULL_HANDLE;
    uint32_t mQueueFamilyIndex = 0;

    goldfish::gvk::CommandPool mCommandPool;
    VkCommandBuffer mCommandBuffer = VK_NULL_HANDLE;

    goldfish::gvk::CommandPool mLoaderCommandPool;
    VkCommandBuffer mLoaderCommandBuffer = VK_NULL_HANDLE;
    std::mutex mQueueLock;

    goldfish::gvk::Sampler mSampler;
    goldfish::gvk::DescriptorSetLayout mDescriptorSetLayout;
    goldfish::gvk::DescriptorPool mDescriptorPool;
    goldfish::gvk::PipelineLayout mMeshPipelineLayout;
    goldfish::gvk::PipelineLayout mScreenPipelineLayout;

    goldfish::gvk::RenderPass mOffscreenRenderPass;
    goldfish::gvk::RenderPass mScreenRenderPass;

    goldfish::gvk::Image mOffscreenColorImage;
    goldfish::gvk::DeviceMemory mOffscreenColorMemory;
    goldfish::gvk::ImageView mOffscreenColorView;

    goldfish::gvk::Image mOffscreenDepthImage;
    goldfish::gvk::DeviceMemory mOffscreenDepthMemory;
    goldfish::gvk::ImageView mOffscreenDepthView;

    goldfish::gvk::Framebuffer mOffscreenFramebuffer;

    goldfish::gvk::Buffer mStagingBuffer;
    goldfish::gvk::DeviceMemory mStagingMemory;

    std::mutex mLoaderQueueMutex;
    std::condition_variable mLoaderQueueCv;
    std::deque<LoaderCommand> mLoaderQueue;
    std::thread mLoaderThread;

    std::mutex mResourceLock;
    int mNextResourceId = 0;
    uint64_t mNextTextureGeneration = 1;
    std::unordered_map<int, MaterialData> mMaterials;
    std::unordered_map<int, MeshData> mMeshes;
    std::unordered_map<int, TextureData> mTextures;
    std::unordered_map<std::string, int> mTextureCache;
    Material mMaterialTextured;
    Texture mDefaultTexture;
    bool mInitialized = false;
};

}  // namespace ver
}  // namespace android
