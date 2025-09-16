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

// Some general notes on debug levels:
// VLOG(1) -- Get FPS from qemu
// VLOG(2) -- Get scaling and timing information
// VLOG(3) -- Add "blue" blocks in the corner for inspecting visual scaling issues.

#define _USE_MATH_DEFINES
#include "android/goldfish/display/PixmanDisplay.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>

#include "absl/log/log.h"

#include "android/base/system/clock.h"

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

namespace {

// Helper function to compute the greatest common divisor.
// Used to simplify the scaling fraction.
int gcd(int a, int b) {
    return std::abs(std::gcd(a, b));
}

// The maximum denominator allowed for a "safe" scaling ratio.
// Ratios with simplified denominators larger than this are likely to cause
// cumulative rounding errors in pixman's fixed-point arithmetic, leading to
// visual artifacts like shearing. A threshold of 16 is a conservative choice,
// allowing for common fractions (1/2, 3/4, 5/8, etc.) while rejecting complex
// ones.
constexpr int kMaxSafeDenominator = 16;

// Checks if a scaling operation from a source dimension to a target dimension
// is likely to be safe from precision-related artifacts.
bool isScalingSafe(int source, int target) {
    if (source == 0 || target == 0) return true;  // No scaling.
    int common = gcd(source, target);
    int denominator = target / common;
    return denominator <= kMaxSafeDenominator;
}

}  // namespace

PixmanDisplay::PixmanDisplay(EventLoop* loop, int id, ::pixman_image_t* image)
        : IDisplay(loop, id, pixman_image_get_width(image), pixman_image_get_height(image))
        , mFrameManager(std::make_unique<PixmanFrameManager>()) {
    updateSourceImage(image);
}

void PixmanDisplay::updateSourceImage(::pixman_image_t* image) {
    DLOG_FIRST_N(WARNING, 100) << "--- WARNING! Reduced performance in debug builds ---";
    auto oldWidth = mWidth;
    auto oldHeight = mHeight;

    // Used to debug issues around scaling, it will create a set of rotating color blocks
    // in the corners that you can use to visually analyze if things "look okay".
    // enable by setting the --vmodule "PixmanDisplay.*=3"
    if (ABSL_VLOG_IS_ON(3)) {
        LOG_FIRST_N(WARNING, 5) << "Adding rotating color blocks in the corners to visually "
                                   "diagnose frame ordering issues.";
        if (mWidth >= 100 && mHeight >= 100) {
            uint64_t frame = seq().sequenceNumber;
            pixman_color_t colors[4] = {
                {0xffff, 0, 0, 0xffff},       // Red
                {0, 0xffff, 0, 0xffff},       // Green
                {0, 0, 0xffff, 0xffff},       // Blue
                {0xffff, 0xffff, 0, 0xffff},  // Yellow
            };

            pixman_rectangle16_t rects[4] = {
                {0, 0, 100, 100},                                          // Top-left
                {int16_t(mWidth - 100), 0, 100, 100},                      // Top-right
                {0, int16_t(mHeight - 100), 100, 100},                     // Bottom-left
                {int16_t(mWidth - 100), int16_t(mHeight - 100), 100, 100}  // Bottom-right
            };

            for (int i = 0; i < 4; ++i) {
                pixman_image_fill_rectangles(PIXMAN_OP_SRC, image, &colors[(frame + i) % 4], 1,
                                             &rects[i]);
            }
        }
    }

    mFrameManager->updateSourceImage(image);
    mWidth = pixman_image_get_width(image);
    mHeight = pixman_image_get_height(image);
    VLOG(2) << "updateSourceImage: " << *this << " to: " << image;
    if (oldWidth != mWidth || oldHeight != mHeight) {
        VLOG(2) << "Informing listeners of change from " << oldWidth << "x" << oldHeight << " to "
                << mWidth << "x" << mHeight << "\n";
        ResizeEventCallbackSource::fireEvent(
                ResizeEvent{mDisplayId, oldWidth, oldHeight, mWidth, mHeight});
    }
}

absl::StatusOr<FrameInfo> PixmanDisplay::getPixels(PixelFormat format, int newWidth, int newHeight,
                                                   int rotation, uint8_t* pixels,
                                                   size_t* cPixels) const {
    // NOTE: We expect newWidth and newHeight to be safe, shearing *WILL* happen if the ratios
    // are not proper.
    absl::Time now = android::base::IClock::host_now();
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

    auto sourceImage = mFrameManager->getRenderableImage();
    ::pixman_image_t* dst_img;
    ::pixman_image_t* src_img = sourceImage.get();
    ::pixman_transform_t transform;

    if (!src_img) {
        return absl::UnavailableError("No frame has been produced yet.");
    }

    uint32_t* pixel = (uint32_t*)pixels;
    // Create the destination image
    dst_img = pixman_image_create_bits(pixmanFmt, newWidth, newHeight, pixel, stride);

    assert(pixman_image_get_width(src_img) == mWidth);
    assert(pixman_image_get_height(src_img) == mHeight);

    double scale_x = (double)mWidth / (double)newWidth;
    double scale_y = (double)mHeight / (double)newHeight;

    VLOG(2) << "Source: " << mWidth << "x" << mHeight << ", dest: " << newWidth << "x" << newHeight
            << ", scale_x: " << scale_x << ", scale_y: " << scale_y;
    // centering/translation logic.
    pixman_transform_init_identity(&transform);
    pixman_transform_translate(&transform, NULL, pixman_double_to_fixed(-0.5),
                               pixman_double_to_fixed(-0.5));
    pixman_transform_scale(&transform, NULL, pixman_double_to_fixed(scale_x),
                           pixman_double_to_fixed(scale_y));
    pixman_transform_translate(&transform, NULL, pixman_double_to_fixed(0.5),
                               pixman_double_to_fixed(0.5));

    // Set the transform and filter on the source image for fast scaling.
    pixman_image_set_filter(src_img, PIXMAN_FILTER_NEAREST, NULL, 0);
    pixman_image_set_transform(src_img, &transform);

    pixman_image_composite(PIXMAN_OP_SRC, src_img, NULL, dst_img, 0, 0, 0, 0, 0, 0, newWidth,
                           newHeight);

    // The buffer is now filled with the scaled and rotated image.
    // The size of the valid pixel data is the required size.
    *cPixels = requiredSize;

    // Clean up
    pixman_image_unref(dst_img);

    absl::MutexLock seqlock(&mSeqAccess);

    VLOG(2) << "Image scaled in: " << (android::base::IClock::host_now() - now);
    return mSeq;
}

void PixmanDisplay::updateSurface(int x, int y, int width, int height) {
    VLOG(2) << "updateSurface " << *this << ", to: (" << x << ", " << y << "), (" << width << "x"
            << height << ")";

    frameReceived();
    if (ABSL_VLOG_IS_ON(1)) {
        mFpsCalculator.addFrame();
        VLOG_EVERY_N_SEC(1, 1) << "Qemu framerate: " << mFpsCalculator.getFps() << " fps";
    }
}

std::pair<int, int> PixmanDisplay::resizeKeepAspectRatio(int desiredWidth, int desiredHeight) {
    if (mWidth <= 0 || mHeight <= 0) {
        return {0, 0};
    }

    // First, calculate the ideal dimensions while preserving aspect ratio.
    int idealWidth, idealHeight;
    // Use 64-bit integers for the cross-multiplication to prevent overflow.
    int64_t h64 = mHeight;
    int64_t w64 = mWidth;

    // Note that we will never scale above display device width and height.
    desiredWidth = std::min<int64_t>(desiredWidth, w64);
    desiredHeight = std::min<int64_t>(desiredHeight, h64);

    if (static_cast<int64_t>(desiredWidth) * h64 < static_cast<int64_t>(desiredHeight) * w64) {
        // Width is the limiting factor.
        idealHeight = static_cast<int>((h64 * desiredWidth) / w64);
        idealWidth = desiredWidth;
    } else {
        // Height is the limiting factor.
        idealWidth = static_cast<int>((w64 * desiredHeight) / h64);
        idealHeight = desiredHeight;
    }

    // Now, check if these ideal dimensions are "safe" for pixman scaling.
    // If not, find the nearest smaller dimensions that are safe.
    // We only need to check the width; the height will be recalculated
    // from the safe width to preserve the aspect ratio.
    int safeWidth = idealWidth;
    if (!isScalingSafe(mWidth, idealWidth)) {
        for (int w_check = idealWidth; w_check > 0; --w_check) {
            if (isScalingSafe(mWidth, w_check)) {
                safeWidth = w_check;
                break;
            }
        }
    }

    // Recalculate the height based on the safe width to maintain aspect ratio.
    int safeHeight = static_cast<int>((h64 * safeWidth) / w64);

    VLOG(2) << "Requested " << desiredWidth << "x" << desiredHeight << ", ideal " << idealWidth
            << "x" << idealHeight << ", snapped to safe " << safeWidth << "x" << safeHeight;

    return {safeWidth, safeHeight};
}

}  // namespace android::goldfish