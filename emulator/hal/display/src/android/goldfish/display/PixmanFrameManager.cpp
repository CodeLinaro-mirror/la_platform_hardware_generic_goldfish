// Copyright 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "android/goldfish/display/PixmanFrameManager.h"

namespace android::goldfish {

void PixmanFrameManager::updateSourceImage(::pixman_image_t* image) {
    absl::MutexLock lock(&mDisplayAccess);
    mStagingImage = PixmanImagePtr(image);
}

PixmanImagePtr PixmanFrameManager::getRenderableImage() {
    PixmanImagePtr local_image;
    {
        absl::MutexLock lock(&mDisplayAccess);
        if (mStagingImage.get()) {
            mCurrentImage = std::move(mStagingImage);
        }
        local_image = mCurrentImage;
    }

    if (!local_image.get()) {
        return local_image;
    }

    // Create a proxy image that shares the bits of the original image.
    // This is a lightweight operation that does not copy the pixel data.
    // The proxy image can have its own transform and filter settings without
    // affecting the original image, making it safe for concurrent rendering.
    auto width = pixman_image_get_width(local_image.get());
    auto height = pixman_image_get_height(local_image.get());
    auto format = pixman_image_get_format(local_image.get());
    auto bits = pixman_image_get_data(local_image.get());
    auto stride = pixman_image_get_stride(local_image.get());

    return PixmanImagePtr(
            pixman_image_create_bits_no_clear(format, width, height, (uint32_t*)bits, stride));
}

}  // namespace android::goldfish
