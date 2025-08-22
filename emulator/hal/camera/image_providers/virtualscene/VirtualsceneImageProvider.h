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

#include <optional>
#include <vector>

#include "android/camera/CameraImageProviderAPI.h"
#include "goldfish/gvk/DeviceDispatch.h"

namespace goldfish::camera_image_providers::virtualscene {

struct VirtualsceneImageProvider {
  struct RenderOutputConfig {
    CameraImageProviderRect size = {};
    imaging::ImageFormat getFormat() const;
  };

  struct CropRegion {
    CameraImageProviderRect offset = {};
    CameraImageProviderRect size = {};
  };

  struct CaptureStreamBuffers {
    gvk::DeviceMemory gpuImageMemory;
    gvk::DeviceMemory dstBufferMemory;
    // if gpuImage is empty it means the framebuffer has
    // exactly the same format and size, so we can copy
    // from it directly.
    gvk::Image gpuImage;
    gvk::Buffer dstBuffer;
    CameraImageProviderRect size = {};
    CropRegion srcCropRegion = {};  // for vkCmdBlitImage
    uint32_t bufferSizeBytes = 0;
    int32_t id = -1;  // CameraImageProviderStreamConfig::id
  };

  VirtualsceneImageProvider() = default;
  ~VirtualsceneImageProvider();

  const char* getId() const;
  int start(const CameraImageProviderStreamConfig* s, unsigned n);
  int capture(const CameraImageProviderCaptureOpts&, const CameraImageProviderStreamCaptureSink,
              void* sinkOpaque, const CameraImageProviderStreamCaptureInfo* sci, unsigned scin);
  void stop();

  static void* create(const CameraImageProviderInfo&);

  static RenderOutputConfig getRenderOutputConfig(const CameraImageProviderStreamConfig* s,
                                                  unsigned n);

  static imaging::ImageFormat getStagingFormat(imaging::ImageFormat);

  // NOTE: all dimensions must be non-zero
  static CropRegion getCropRegion(CameraImageProviderRect srcSize, CameraImageProviderRect dstSize);

  static const CaptureStreamBuffers* findCaptureStreamBuffers(int32_t id,
                                                              const CaptureStreamBuffers* a,
                                                              size_t n);

 private:
  struct CaptureSession {
    std::vector<CaptureStreamBuffers> captureStreamBuffers;
    std::vector<uint8_t> yuvConversionBuffer;
    gvk::DeviceMemory framebufferImageMemory;
    gvk::Image framebufferImage;
    gvk::ImageView framebufferImageView;
    gvk::RenderPass renderPass;
    gvk::Framebuffer framebuffer;
    gvk::Pipeline pipeline;
    VkCommandBuffer renderCmdBuf = VK_NULL_HANDLE;
    CameraImageProviderRect framebufferSize;
    uint8_t frameCounter = 0;
  };

  bool captureImpl(const CaptureSession&, CameraImageProviderStreamCaptureSink, void* sinkOpaque,
                   const CameraImageProviderStreamCaptureInfo* sci, unsigned scin,
                   uint8_t* yuvConversionBuffer);

  bool updateUniformData(CameraImageProviderRect framebufferSize, float angle);

  VkCommandBuffer recordCommandBuffer(const CaptureSession&,
                                      const CameraImageProviderStreamCaptureInfo* sci,
                                      unsigned sciSize);

  gvk::DeviceDispatch::Ptr mDeviceDispatch;
  gvk::CommandPool mCommandPool;
  gvk::DeviceMemory mVerticesMem;
  gvk::DeviceMemory mIndicesMem;
  gvk::DeviceMemory mUniformMem;
  gvk::Buffer mVerticesBuf;
  gvk::Buffer mIndicesBuf;
  gvk::Buffer mUniformBuf;
  gvk::ShaderModule mVertexShaderModule;
  gvk::ShaderModule mFragmentShaderModule;
  gvk::DescriptorSetLayout mDescriptorSetLayout;
  gvk::DescriptorPool mDescriptorPool;
  gvk::PipelineLayout mPipelineLayout;
  VkDescriptorSet mDescriptorSet = VK_NULL_HANDLE;

  VkQueue mGraphicsQueue = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties mMemoryProperties = {};
  gvk::DeviceQueueLocation mGraphicsQueueLocation;

  std::optional<CaptureSession> mCaptureSession;
};

}  // namespace goldfish::camera_image_providers::virtualscene