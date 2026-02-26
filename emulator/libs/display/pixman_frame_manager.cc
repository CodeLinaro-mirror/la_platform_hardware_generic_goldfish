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

#include "goldfish/display/pixman_frame_manager.h"

#include "absl/hash/hash.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"

#include "goldfish/display/pixman_image_ptr.h"

namespace {
// Calculates a hash of the pixel data of a pixman_image_t, used for debugging frame issues.
size_t CalculateHash(::pixman_image_t* image) {
    if (!image) {
        return 0;
    }
    auto height = pixman_image_get_height(image);
    auto stride = pixman_image_get_stride(image);
    const auto* bits = reinterpret_cast<const char*>(pixman_image_get_data(image));
    if (!bits) {
        return 0;
    }

    const absl::Hash<absl::string_view> hasher;
    return hasher(absl::string_view(bits, static_cast<size_t>(height) * stride));
}
}  // namespace

namespace goldfish::display {

void PixmanFrameManager::UpdateSurface() {
    const absl::MutexLock lock(display_access_);
    auto height = pixman_image_get_height(current_image_.get());
    memcpy(pixman_image_get_data(current_image_.get()), src_bits_,
           static_cast<size_t>(height) * stride_);
}

void PixmanFrameManager::UpdateSourceImage(::pixman_image_t* image) {
    if (pixman_image_get_depth(image) < current_pixel_depth_) {
        // QEMU delivers two display streams: a 24bpp stream for the "disconnected"
        // display state and a 32bpp stream for the active Android guest
        // framebuffer.

        // Before the guest UI is active, only the 24bpp stream is sent. However,
        // once the guest activates, QEMU begins sending the 32bpp stream *in
        // addition to* the 24bpp stream, resulting in an interleaved delivery
        // of both frame types. We are going to discard the 24bpp frames.
        VLOG(2) << "Not accepting image with pixel depth: " << pixman_image_get_depth(image)
                << ", expecting: " << current_pixel_depth_;
        return;
    }

    current_pixel_depth_ = pixman_image_get_depth(image);

    VLOG(3) << "updateSourceImage pixel hash: " << CalculateHash(image);
    // Create a deep copy of the image to prevent race conditions. This is the
    // slow part and happens outside the lock.
    auto width = pixman_image_get_width(image);
    auto height = pixman_image_get_height(image);
    auto format = pixman_image_get_format(image);
    auto* src_bits = pixman_image_get_data(image);
    auto stride = pixman_image_get_stride(image);
    const PixmanImagePtr new_image(
            pixman_image_create_bits(format, width, height, nullptr, stride));
    // Lock and swap the pointer. This is very fast.
    const absl::MutexLock lock(display_access_);
    // when only the content changes, we need to
    // preserve the continuity of frame by copying
    // over the current content to the next frame;
    // otherwise, it will have the appearance of out
    // of order frames.
    if (current_image_.get()) {
        auto* curr_image = current_image_.get();
        auto curr_width = pixman_image_get_width(curr_image);
        auto curr_height = pixman_image_get_height(curr_image);
        auto curr_format = pixman_image_get_format(curr_image);
        auto curr_stride = pixman_image_get_stride(curr_image);
        if (curr_width == width && curr_height == height && curr_format == format &&
            curr_stride == stride) {
            src_bits = pixman_image_get_data(current_image_.get());
        }
    }

    memcpy(pixman_image_get_data(new_image.get()), src_bits, static_cast<size_t>(height) * stride);
    src_bits = pixman_image_get_data(image);
    src_bits_ = src_bits;
    stride_ = stride;
    current_image_ = new_image;
}

PixmanImagePtr PixmanFrameManager::GetRenderableImage() {
    const absl::MutexLock lock(display_access_);
    return current_image_;
}

}  // namespace goldfish::display
