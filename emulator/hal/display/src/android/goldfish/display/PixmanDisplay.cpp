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

// This must be first to get M_PI.
#define _USE_MATH_DEFINES
#include <cmath>

#include "android/goldfish/display/PixmanDisplay.h"

#include <cstddef>
#include <cstdint>

#include "absl/log/log.h"

#include "qemu/atomic.hpp"

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "pixman.h"
// IWYU pragma: end_keep
// clang-format on
}

namespace {

static pixman_format_code_t pixmanFormat(const ::android::goldfish::PixelFormat& format) {
    switch (format) {
        case ::android::goldfish::PixelFormat::RGBA8888:
            return PIXMAN_a8r8g8b8;
        case ::android::goldfish::PixelFormat::RGB888:
            return PIXMAN_b8g8r8;
        default:
            return PIXMAN_a8r8g8b8;
    }
}
}  // namespace

namespace android::goldfish {

PixmanImagePtr::PixmanImagePtr(::pixman_image_t* image) : mImage(image) {
    if (image) {
        ::pixman_image_ref(image);
    }
}

PixmanImagePtr::~PixmanImagePtr() {
    if (mImage) {
        ::pixman_image_unref(mImage);
    }
}

PixmanImagePtr::PixmanImagePtr(PixmanImagePtr&& other) noexcept : mImage(other.mImage) {
    other.mImage = nullptr;
}

PixmanImagePtr& PixmanImagePtr::operator=(PixmanImagePtr&& other) noexcept {
    if (this != &other) {
        if (mImage) {
            ::pixman_image_unref(mImage);
        }
        mImage = other.mImage;
        other.mImage = nullptr;
    }
    return *this;
}

::pixman_image_t* PixmanImagePtr::get() const {
    return mImage;
}

::pixman_image_t* PixmanImagePtr::operator->() const {
    return mImage;
}

PixmanDisplay::PixmanDisplay(int id, ::pixman_image_t* image)
    : IDisplay(id, pixman_image_get_width(image), pixman_image_get_height(image)) {
    updateSourceImage(image);
}

void PixmanDisplay::updateSourceImage(::pixman_image_t* image) {
    absl::MutexLock lock(&mDisplayAccess);
    mSourceImage = PixmanImagePtr(image);
    mWidth = pixman_image_get_width(image);
    mHeight = pixman_image_get_height(image);
}

absl::StatusOr<FrameInfo> PixmanDisplay::getPixels(PixelFormat format, int newWidth, int newHeight,
                                                   int rotation, uint8_t* pixels,
                                                   size_t* cPixels) const {
    auto pixmanFmt = pixmanFormat(format);
    auto bpp = PIXMAN_FORMAT_BPP(pixmanFmt);
    auto stride =
            ((newWidth * bpp + sizeof(uint32_t) * CHAR_BIT - 1) / (sizeof(uint32_t) * CHAR_BIT)) *
            sizeof(uint32_t);
    size_t requiredSize = newHeight * stride;

    if (requiredSize > *cPixels) {
        auto old = *cPixels;
        *cPixels = requiredSize;
        return absl::FailedPreconditionError(
                absl::StrFormat("Buffer too small; need %u bytes, have %u", requiredSize, old));
    }

    absl::MutexLock lock(&mDisplayAccess);
    ::pixman_image_t* dst_img;
    ::pixman_image_t* src_img = mSourceImage.get();
    ::pixman_transform_t transform;

    if (!src_img) {
        return absl::UnavailableError("No frame has been produced yet.");
    }

    uint32_t* pixel = (uint32_t*)pixels;
    // Create the destination image
    dst_img = pixman_image_create_bits(pixmanFmt, newWidth, newHeight, pixel, stride);

    assert(pixman_image_get_width(src_img) == mWidth);
    assert(pixman_image_get_height(src_img) == mHeight);

    // Set up the transformation (scale and rotate)
    pixman_transform_init_identity(&transform);  // Start with identity

    // Apply scaling
    pixman_transform_scale(&transform, NULL, pixman_double_to_fixed((double)newWidth / mWidth),
                           pixman_double_to_fixed((double)newHeight / mHeight));

    // Apply rotation around the center
    double angleRadians = rotation * M_PI / 180.0;
    pixman_fixed_t cos_val = pixman_double_to_fixed(cos(angleRadians));
    pixman_fixed_t sin_val = pixman_double_to_fixed(sin(angleRadians));

    pixman_transform_translate(&transform, NULL, pixman_int_to_fixed(newWidth / 2),
                               pixman_int_to_fixed(newHeight / 2));
    pixman_transform_rotate(&transform, NULL, cos_val, sin_val);
    pixman_transform_translate(&transform, NULL, pixman_int_to_fixed(-newWidth / 2),
                               pixman_int_to_fixed(-newHeight / 2));
    pixman_image_set_transform(dst_img, &transform);

    pixman_image_composite(PIXMAN_OP_SRC, src_img, NULL, dst_img, 0, 0, 0, 0, 0, 0, mWidth,
                           mHeight);

    int dheight = pixman_image_get_height(dst_img);
    int dstride = pixman_image_get_stride(dst_img);

    assert(dstride != 0);

    // Calculate total size to copy (height * stride)
    *cPixels = dheight * dstride;
    memcpy(pixels, pixman_image_get_data(dst_img), *cPixels);

    // Clean up
    pixman_image_unref(dst_img);

    absl::MutexLock seqlock(&mSeqAccess);
    return mSeq;
}

void PixmanDisplay::updateSurface(int x, int y, int width, int height) {
    frameReceived();
}

}  // namespace android::goldfish
