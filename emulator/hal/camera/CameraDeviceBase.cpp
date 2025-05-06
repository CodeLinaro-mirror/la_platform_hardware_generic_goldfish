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

#include "android/camera/CameraDeviceBase.h"

#include "absl/log/log.h"

namespace goldfish::devices::camera {

CameraDeviceBase::CameraDeviceBase(SocketPtr socket, void* imageProvider,
                                   const CameraImageProviderVtbl& vtbl, GpuDetailsPtr gpuDetails)
        : CameraProtocolBase(std::move(socket))
        , mImageProvider(imageProvider)
        , mImageProviderVtbl(vtbl)
        , mGpuDetails(std::move(gpuDetails)) {}

CameraDeviceBase::~CameraDeviceBase() {
    (mImageProviderVtbl.dctor)(mImageProvider);
}

bool CameraDeviceBase::startCapturingImpl(const CameraImageProviderStreamConfig* streams,
                                          const size_t nStreams) const {
    const int ret = (mImageProviderVtbl.start)(mImageProvider, streams, nStreams);
    if (ret) {
        VLOG(1) << (mImageProviderVtbl.getId)(mImageProvider) << "::start failed with ret=" << ret;
        return false;
    } else {
        return true;
    }
}

bool CameraDeviceBase::captureImpl(const CameraImageProviderCaptureOpts& opts,
                                   const CameraImageProviderStreamCaptureInfo* scis,
                                   const size_t scisSize) {
    const int ret = (mImageProviderVtbl.capture)(mImageProvider, &opts, &imageSinkStatic, this,
                                                 scis, scisSize);
    if (ret) {
        VLOG(1) << (mImageProviderVtbl.getId)(mImageProvider)
                << "::capture failed with ret=" << ret;
        return false;
    } else {
        return true;
    }
}

void CameraDeviceBase::stopCapturingImpl() const {
    (mImageProviderVtbl.stop)(mImageProvider);
}

uint32_t CameraDeviceBase::aFormatToFourCC(uint32_t androidFormat) const {
    return mGpuDetails->aFormatToFourCC(androidFormat);
}

int CameraDeviceBase::imageSink(const CameraImageProviderStreamCaptureInfo& sci,
                                const void* framebufferPtr, const size_t framebufferSize) const {
    return mGpuDetails->imageSink(*sci.cfg, *static_cast<const std::string_view*>(sci.bufOpaque),
                                  framebufferPtr, framebufferSize);
}

int CameraDeviceBase::imageSinkStatic(void* that, const CameraImageProviderStreamCaptureInfo* sci,
                                      const void* framebufferPtr, const size_t framebufferSize) {
    return static_cast<CameraDeviceBase*>(that)->imageSink(*sci, framebufferPtr, framebufferSize);
}

}  // namespace goldfish::devices::camera
