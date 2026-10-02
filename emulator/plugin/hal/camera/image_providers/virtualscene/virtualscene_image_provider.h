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

#include <cstdint>
#include <optional>
#include <vector>

#include "android/camera/camera_image_provider_api.h"
#include "ver/virtual_environment_renderer.h"

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
        // If dstBuffer is empty, the framebuffer has the exact same format and size,
        // so we can read from the VER framebuffer directly.
        mutable std::vector<uint8_t> dstBuffer;
        CameraImageProviderRect size = {};
        CropRegion srcCropRegion = {};
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
    static CropRegion getCropRegion(CameraImageProviderRect srcSize,
                                    CameraImageProviderRect dstSize);

    static const CaptureStreamBuffers* findCaptureStreamBuffers(int32_t id,
                                                                const CaptureStreamBuffers* a,
                                                                size_t n);

  private:
    struct CaptureSession {
        std::vector<CaptureStreamBuffers> captureStreamBuffers;
        std::vector<uint8_t> yuvConversionBuffer;
        VerRenderViewHandle view = VER_INVALID_HANDLE;
        CameraImageProviderRect framebuffer_size = {};
    };

    bool captureImpl(const CaptureSession&, CameraImageProviderStreamCaptureSink, void* sinkOpaque,
                     const CameraImageProviderStreamCaptureInfo* sci, unsigned scin,
                     uint8_t* yuvConversionBuffer);

    VerSceneHandle mScene = VER_INVALID_HANDLE;

    std::optional<CaptureSession> mCaptureSession;
};

}  // namespace goldfish::camera_image_providers::virtualscene