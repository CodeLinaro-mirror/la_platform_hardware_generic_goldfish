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
#include "goldfish/display/pixman_display.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>

#include "absl/log/log.h"

#include "android/base/clock.h"
#include "goldfish/display/write_png.h"

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "pixman.h"
// IWYU pragma: end_keep
// clang-format on
}

namespace goldfish::display {

namespace {

static pixman_format_code_t pixmanFormat(const PixelFormat& format) {
    switch (format) {
    case PixelFormat::RGBA8888:
        return PIXMAN_a8r8g8b8;
    case PixelFormat::RGB888:
        return PIXMAN_b8g8r8;
    case PixelFormat::PNG:
        return PIXMAN_a8b8g8r8;
    default:
        return PIXMAN_a8r8g8b8;
    }
}

// Helper function to compute the greatest common divisor.
// Used to simplify the scaling fraction.
int gcd(int a, int b) {
    return std::abs(std::gcd(a, b));
}

// The maximum denominator allowed for a "safe" scaling ratio.
// Ratios with simplified denominators larger than this are likely to cause
// cumulative rounding errors in pixman's fixed-point arithmetic, leading to
// visual artifacts like shearing. A threshold of 1024 is sufficient to prevent
// visible rounding errors in 16.16 fixed-point math while allowing for
// much closer fits to the desired dimensions.
constexpr int kMaxSafeDenominator = 1024;

// Checks if a scaling operation from a source dimension to a target dimension
// is likely to be safe from precision-related artifacts.
bool isScalingSafe(int source, int target) {
    if (target % 4) {
        // Pixman requires the stride (in bytes) to be a mulple of 4 bytes,
        // so for RGB (3 bytes per pixel) we need the width to be a multiple
        // of 4 as well.
        return false;
    }
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

PixmanDisplay::PixmanDisplay(EventLoop* loop, int id, PixmanImagePtr image)
        : PixmanDisplay(loop, id, image.get()) {}

void PixmanDisplay::updateSourceImage(::pixman_image_t* image) {
    DLOG_FIRST_N(WARNING, 2) << "--- WARNING! Reduced performance in debug builds ---";
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
        ResizeEventCallbackSource::FireEvent(
                ResizeEvent{mDisplayId, oldWidth, oldHeight, mWidth, mHeight});
    }
}

absl::StatusOr<FrameInfo> PixmanDisplay::getPixels(PixelFormat format, int newWidth, int newHeight,
                                                   ImageRotation rotation, uint8_t* pixels,
                                                   size_t* cPixels) const {
    // NOTE: We expect newWidth and newHeight to be safe, shearing *WILL* happen if the ratios
    // are not proper.
    absl::Time now = android::base::IClock::HostNow();
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

    uint32_t* pixel = (uint32_t*)pixels;
    // Create the destination image
    const PixmanImagePtr dst_img(
            pixman_image_create_bits(pixmanFmt, newWidth, newHeight, pixel, stride));

    auto sourceImage = mFrameManager->getRenderableImage();
    ::pixman_image_t* src_img = sourceImage.get();
    ::pixman_transform_t transform;

    if (!src_img) {
        return absl::UnavailableError("No frame has been produced yet.");
    }

    assert(pixman_image_get_width(src_img) == mWidth);
    assert(pixman_image_get_height(src_img) == mHeight);

    // If we are rotated 90/270, the destination width fits the source height.
    bool needsDimensionSwap =
            (rotation == ImageRotation::kRotation90 || rotation == ImageRotation::kRotation270);
    double scale_x = needsDimensionSwap ? (double)mHeight / newWidth : (double)mWidth / newWidth;
    double scale_y = needsDimensionSwap ? (double)mWidth / newHeight : (double)mHeight / newHeight;

    VLOG(2) << "Source: " << mWidth << "x" << mHeight << ", dest: " << newWidth << "x" << newHeight
            << ", rotation: " << static_cast<int>(rotation) << ", scale_x: " << scale_x
            << ", scale_y: " << scale_y;
    // centering/translation logic.
    pixman_transform_init_identity(&transform);

    if (rotation != ImageRotation::kRotation0) {
        // We want to rotate around the center.
        // The transform maps destination -> source.
        const double radians = static_cast<int>(rotation) * M_PI / 180.0;

        // 1. Translate destination center to origin
        pixman_transform_translate(&transform, NULL,
                                   pixman_double_to_fixed(-newWidth / 2.0),    // move left
                                   pixman_double_to_fixed(-newHeight / 2.0));  // move up

        // 2. Rotate (destination to source)
        // A clockwise rotation of the coordinate system (destination->source)
        // results in a counter-clockwise rotation of the image content.
        pixman_transform_rotate(&transform, NULL,
                                pixman_double_to_fixed(cos(radians)),   // cos(theta)
                                pixman_double_to_fixed(sin(radians)));  // sin(theta)

        // 3. Translate back to source center
        pixman_transform_translate(&transform, NULL, pixman_double_to_fixed(mWidth / 2.0),
                                   pixman_double_to_fixed(mHeight / 2.0));
    }

    // Scaling logic.
    // Shift the whole image by -0.5 so we are looking at the center of each pixel
    // instead of the edge. This is important for scaling to be smooth.
    pixman_transform_translate(&transform, NULL, pixman_double_to_fixed(-0.5),
                               pixman_double_to_fixed(-0.5));
    // Perform the scaling.
    pixman_transform_scale(&transform, NULL, pixman_double_to_fixed(scale_x),
                           pixman_double_to_fixed(scale_y));
    // Move the image back to the original position (+0.5).
    pixman_transform_translate(&transform, NULL, pixman_double_to_fixed(0.5),
                               pixman_double_to_fixed(0.5));

    // Set the transform and filter on the source image for fast scaling.
    pixman_image_set_filter(src_img, PIXMAN_FILTER_NEAREST, NULL, 0);
    pixman_image_set_transform(src_img, &transform);

    pixman_image_composite(PIXMAN_OP_SRC, src_img, NULL, dst_img.get(), 0, 0, 0, 0, 0, 0, newWidth,
                           newHeight);

    // The buffer is now filled with the scaled and rotated image.
    // The size of the valid pixel data is the required size.
    *cPixels = requiredSize;
    VLOG(2) << "getPixels source {w:" << mWidth << " h:" << mHeight << "}, dest {w:" << newWidth
            << " h:" << newHeight << "} px_size=" << *cPixels;

    if (format == PixelFormat::PNG) {
        std::vector<uint8_t> png_buffer_vec;
        constexpr int nChannels = 4;
        if (!write_png(nChannels, newWidth, newHeight, pixels, png_buffer_vec)) {
            return absl::UnavailableError("Failed to create PNG screenshot!");
        }
        size_t png_size = png_buffer_vec.size();
        memcpy(pixels, png_buffer_vec.data(), png_size);
        assert(png_size <= *cPixels);
        *cPixels = png_size;
    }

    absl::MutexLock seqlock(&mSeqAccess);

    VLOG(2) << "Image scaled in: " << (android::base::IClock::HostNow() - now);
    return mSeq;
}

void PixmanDisplay::updateSurface(int x, int y, int width, int height) {
    VLOG(2) << "updateSurface " << *this << ", to: (" << x << ", " << y << "), (" << width << "x"
            << height << ")";

    mFrameManager->updateSurface();
    frameReceived();
    if (ABSL_VLOG_IS_ON(2)) {
        mFpsCalculator.AddFrame();
        VLOG_EVERY_N_SEC(2, 1) << "Qemu framerate: " << mFpsCalculator.GetFps() << " fps";
    }
}

std::pair<int, int> PixmanDisplay::resizeKeepAspectRatio(int desiredWidth, int desiredHeight) {
    auto fit = calculateLogicalFit(desiredWidth, desiredHeight);
    if (fit.width == 0 || fit.height == 0) {
        return {0, 0};
    }

    // The physical width of the destination buffer is always fit.width.
    // To ensure scaling is safe, we check the ratio between the source physical
    // stride and this destination physical width.
    int sourcePhysicalStride = fit.swapped ? mHeight : mWidth;
    int safeWidth = fit.width;

    if (!isScalingSafe(sourcePhysicalStride, safeWidth)) {
        for (int w_check = safeWidth; w_check > 0; --w_check) {
            if (isScalingSafe(sourcePhysicalStride, w_check)) {
                safeWidth = w_check;
                break;
            }
        }
    }

    // Logically oriented source dimensions.
    int64_t sWidth = fit.swapped ? mHeight : mWidth;
    int64_t sHeight = fit.swapped ? mWidth : mHeight;

    // Recalculate the logical height based on the safe logical width to
    // maintain the aspect ratio.
    int safeHeight = static_cast<int>((sHeight * safeWidth) / sWidth);

    VLOG(2) << "Requested " << desiredWidth << "x" << desiredHeight << ", ideal " << fit.width
            << "x" << fit.height << ", snapped to safe " << safeWidth << "x" << safeHeight;

    return {safeWidth, safeHeight};
}

}  // namespace goldfish::display
