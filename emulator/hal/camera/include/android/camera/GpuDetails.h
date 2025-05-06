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
#include <string_view>

#include "android/camera/CameraImageProvider.h"

namespace goldfish::devices::camera {

/*
 * Interface to abstract GPU-specific details for the camera device.
 * It allows to decouple camera device from a specific GPU implementation.
 */
struct IGpuDetails {
    virtual ~IGpuDetails() = default;

    /*
     * Returns the GPU specific fourcc format for PixelFormat in Android.
     */
    virtual uint32_t aFormatToFourCC(uint32_t androidFormat) const = 0;

    /*
     * Sends an image represented by `cfg`, `framebuffer` and `framebufferSize`
     * into the GPU using the handle stored in `handleStr`.
     *
     * Returns non-zero if an error.
     */
    virtual int imageSink(const CameraImageProviderStreamConfig& cfg, std::string_view handleStr,
                          const void* framebuffer, size_t framebufferSize) const = 0;
};

using GpuDetailsPtr = std::shared_ptr<IGpuDetails>;

}  // namespace goldfish::devices::camera
