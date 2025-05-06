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

#include "android/camera/CameraImageProvider.h"
#include "android/camera/CameraProtocolBase.h"
#include "android/camera/GpuDetails.h"

namespace goldfish::devices::camera {

struct CameraDeviceBase : public CameraProtocolBase {
    CameraDeviceBase(SocketPtr socket, void* imageProvider, const CameraImageProviderVtbl& vtbl,
                     GpuDetailsPtr gpuDetails);

    ~CameraDeviceBase() override;

  protected:
    bool startCapturingImpl(const CameraImageProviderStreamConfig* streams,
                            const size_t nStreams) const;
    bool captureImpl(const CameraImageProviderCaptureOpts& opts,
                     const CameraImageProviderStreamCaptureInfo* scis, const size_t scisSize);
    void stopCapturingImpl() const;
    uint32_t aFormatToFourCC(uint32_t androidFormat) const;

  private:
    int imageSink(const CameraImageProviderStreamCaptureInfo& sci, const void* framebufferPtr,
                  const size_t framebufferSize) const;
    static int imageSinkStatic(void* that, const CameraImageProviderStreamCaptureInfo* sci,
                               const void* framebufferPtr, const size_t framebufferSize);

    void* const mImageProvider;
    const CameraImageProviderVtbl& mImageProviderVtbl;
    const GpuDetailsPtr mGpuDetails;
};

}  // namespace goldfish::devices::camera
