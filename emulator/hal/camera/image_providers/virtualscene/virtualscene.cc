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

#include "goldfish/camera_image_providers/virtualscene/virtualscene.h"

#include "emulator/hal/camera/image_providers/virtualscene/VirtualsceneImageProvider.h"
#include "goldfish/devices/camera/image_provider_cpp_adapter.h"

using goldfish::devices::camera::ImageProviderCppAdapter;

namespace goldfish::camera_image_providers::virtualscene {

bool getImageProviderInfo(CameraImageProviderInfo* dst, const bool isBackFacing) {
    static const CameraImageProviderRect supportedFrameSizes[] = {
        {
            .width = 640,
            .height = 480,
        },
        {
            .width = 352,
            .height = 288,
        },
        {
            .width = 320,
            .height = 240,
        },
        {
            .width = 176,
            .height = 144,
        },
        {
            .width = 1280,
            .height = 720,
        },
        {
            .width = 1280,
            .height = 960,
        },
    };

    static const CameraImageProviderInfoVtbl vtbl = {
        .create = &ImageProviderCppAdapter<VirtualsceneImageProvider>::create,
        .createArgDctor = nullptr,
    };

    *dst = (CameraImageProviderInfo){
        .vtbl = &vtbl,
        .supportedFrameSizes = supportedFrameSizes,
        .createArg = nullptr,
        .supportedFrameSizesNum = sizeof(supportedFrameSizes) / sizeof(supportedFrameSizes[0]),
        .isBackFacing = static_cast<uint8_t>(isBackFacing),
        .needFreeSupportedSizes = 0,
    };

    return true;
}

}  // namespace goldfish::camera_image_providers::virtualscene