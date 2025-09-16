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

#include <pixman.h>

#include "absl/hash/hash.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"

namespace {
// Calculates a hash of the pixel data of a pixman_image_t, used for debugging frame issues.
size_t calculateHash(::pixman_image_t* image) {
    if (!image) {
        return 0;
    }
    auto height = pixman_image_get_height(image);
    auto stride = pixman_image_get_stride(image);
    auto* bits = reinterpret_cast<const char*>(pixman_image_get_data(image));
    if (!bits) {
        return 0;
    }

    absl::Hash<absl::string_view> hasher;
    return hasher(absl::string_view(bits, height * stride));
}
}  // namespace

namespace android::goldfish {

void PixmanFrameManager::updateSourceImage(::pixman_image_t* image) {
    if (pixman_image_get_depth(image) < mCurrentPixelDepth) {
        // QEMU delivers two display streams: a 24bpp stream for the "disconnected"
        // display state and a 32bpp stream for the active Android guest
        // framebuffer.

        // Before the guest UI is active, only the 24bpp stream is sent. However,
        // once the guest activates, QEMU begins sending the 32bpp stream *in
        // addition to* the 24bpp stream, resulting in an interleaved delivery
        // of both frame types. We are going to discard the 24bpp frames.
        VLOG(1) << "Not accepting image with pixel depth: " << pixman_image_get_depth(image)
                << ", expecting: " << mCurrentPixelDepth;
        return;
    }

    mCurrentPixelDepth = pixman_image_get_depth(image);

    VLOG(3) << "updateSourceImage pixel hash: " << calculateHash(image);
    // Create a deep copy of the image to prevent race conditions. This is the
    // slow part and happens outside the lock.
    auto width = pixman_image_get_width(image);
    auto height = pixman_image_get_height(image);
    auto format = pixman_image_get_format(image);
    auto* src_bits = pixman_image_get_data(image);
    auto stride = pixman_image_get_stride(image);

    auto new_image =
            PixmanImagePtr(pixman_image_create_bits(format, width, height, nullptr, stride));
    memcpy(pixman_image_get_data(new_image.get()), src_bits, height * stride);

    // Lock and swap the pointer. This is very fast.
    absl::MutexLock lock(&mDisplayAccess);
    mCurrentImage = new_image;
}

PixmanImagePtr PixmanFrameManager::getRenderableImage() {
    PixmanImagePtr local_image;
    {
        // Lock and copy the smart pointer. This is very fast.
        absl::MutexLock lock(&mDisplayAccess);
        local_image = mCurrentImage;
    }

    if (!local_image.get()) {
        return local_image;
    }

    VLOG(3) << "getRenderableImage pixel hash: " << calculateHash(local_image.get());

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
