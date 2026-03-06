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

// Some general notes on debug levels:
// VLOG(1) -- Get FPS from qemu
// VLOG(2) -- Get scaling and timing information
// VLOG(3) -- Add "blue" blocks in the corner for inspecting visual scaling issues.

#include "goldfish/display/pixman_display.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <numeric>

#include "absl/log/log.h"

#include "android/base/clock.h"
#include "goldfish/display/imaging.h"
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

#include <cstring>  // Required for std::memcpy
#include <vector>

constexpr double kPi = std::numbers::pi;

class ImageRotator {
  public:
    /**
     * Rotates an RGB or RGBA buffer counter-clockwise.
     * * @param src The source buffer (must be valid).
     * @param dst The destination buffer (must be allocated with correct size).
     * @param width The width of the source image.
     * @param height The height of the source image.
     * @param channels Number of channels per pixel (3 or 4).
     * @param rotation The angle to rotate.
     */
    static void Rotate(const uint8_t* src, uint8_t* dst, int width, int height, int channels,
                       ImageRotation rotation) {
        // Basic safety checks
        if (!src || !dst || channels < 1) return;

        // Determine destination dimensions for stride calculation
        // Note: We don't need to return these, but we need them for the math.
        int out_width;
        if (rotation == ImageRotation::kRotation180) {
            out_width = width;
        } else {
            out_width = height;  // For 90 and 270, width and height swap
        }

        switch (rotation) {
        case ImageRotation::kRotation90: {
            // Source (x, y) -> Dest (y, width - 1 - x)
            for (int y = 0; y < height; ++y) {
                const size_t src_row_offset = static_cast<size_t>(y) * width;

                for (int x = 0; x < width; ++x) {
                    const int dst_row = width - 1 - x;
                    const int dst_col = y;

                    const size_t src_index = (src_row_offset + x) * channels;
                    const size_t dst_index =
                            (static_cast<size_t>(dst_row) * out_width + dst_col) * channels;

                    std::memcpy(dst + dst_index, src + src_index, channels);
                }
            }
            break;
        }

        case ImageRotation::kRotation180: {
            // 180 degree rotation is simply reading the array backwards pixel by pixel.
            // Dest Index = TotalPixels - 1 - SourceIndex
            const size_t total_pixels = static_cast<size_t>(width) * height;

            for (size_t i = 0; i < total_pixels; ++i) {
                const size_t src_index = i * channels;
                const size_t dst_index = (total_pixels - 1 - i) * channels;

                std::memcpy(dst + dst_index, src + src_index, channels);
            }
            break;
        }

        case ImageRotation::kRotation270: {
            // Source (x, y) -> Dest (height - 1 - y, x)
            for (int y = 0; y < height; ++y) {
                const size_t src_row_offset = static_cast<size_t>(y) * width;

                for (int x = 0; x < width; ++x) {
                    const int dst_row = x;
                    const int dst_col = height - 1 - y;

                    const size_t src_index = (src_row_offset + x) * channels;
                    const size_t dst_index =
                            (static_cast<size_t>(dst_row) * out_width + dst_col) * channels;

                    std::memcpy(dst + dst_index, src + src_index, channels);
                }
            }
            break;
        }
        }
    }
};

constexpr bool kNoScaling = true;

pixman_format_code_t PixmanFormat(const PixelFormat& format) {
    switch (format) {
    case PixelFormat::kRgba8888:
        return PIXMAN_a8r8g8b8;
    case PixelFormat::kRgb888:
        return PIXMAN_b8g8r8;
    case PixelFormat::kPng:
        return PIXMAN_a8b8g8r8;
    default:
        return PIXMAN_a8r8g8b8;
    }
}

// Helper function to compute the greatest common divisor.
// Used to simplify the scaling fraction.
int Gcd(int a, int b) {
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
bool IsScalingSafe(int source, int target) {
    if (target % 4) {
        // Pixman requires the stride (in bytes) to be a mulple of 4 bytes,
        // so for RGB (3 bytes per pixel) we need the width to be a multiple
        // of 4 as well.
        return false;
    }
    if (source == 0 || target == 0) return true;  // No scaling.
    const int common = Gcd(source, target);
    const int denominator = target / common;
    return denominator <= kMaxSafeDenominator;
}

}  // namespace

PixmanDisplay::PixmanDisplay(EventLoop* loop, int id, ::pixman_image_t* image)
        : IDisplay(loop, id, pixman_image_get_width(image), pixman_image_get_height(image))
        , frame_manager_(std::make_unique<PixmanFrameManager>()) {
    UpdateSourceImage(image);
}

PixmanDisplay::PixmanDisplay(EventLoop* loop, int id, const PixmanImagePtr& image)
        : PixmanDisplay(loop, id, image.get()) {}

void PixmanDisplay::UpdateSourceImage(::pixman_image_t* image) {
    DLOG_FIRST_N(WARNING, 2) << "--- WARNING! Reduced performance in debug builds ---";
    const Dimensions dims = GetDimensions();
    auto old_width = dims.width;
    auto old_height = dims.height;

    // Used to debug issues around scaling, it will create a set of rotating color blocks
    // in the corners that you can use to visually analyze if things "look okay".
    // enable by setting the --vmodule "PixmanDisplay.*=3"
    if (ABSL_VLOG_IS_ON(3)) {
        LOG_FIRST_N(WARNING, 5) << "Adding rotating color blocks in the corners to visually "
                                   "diagnose frame ordering issues.";
        if (dims.width >= 100 && dims.height >= 100) {
            const uint64_t frame = Seq().sequence_number;
            const pixman_color_t colors[4] = {
                {0xffff, 0, 0, 0xffff},       // Red
                {0, 0xffff, 0, 0xffff},       // Green
                {0, 0, 0xffff, 0xffff},       // Blue
                {0xffff, 0xffff, 0, 0xffff},  // Yellow
            };

            const pixman_rectangle16_t rects[4] = {
                {0, 0, 100, 100},                                        // Top-left
                {static_cast<int16_t>(dims.width - 100), 0, 100, 100},   // Top-right
                {0, static_cast<int16_t>(dims.height - 100), 100, 100},  // Bottom-left
                {static_cast<int16_t>(dims.width - 100), static_cast<int16_t>(dims.height - 100),
                 100, 100}  // Bottom-right
            };

            for (int i = 0; i < 4; ++i) {
                pixman_image_fill_rectangles(PIXMAN_OP_SRC, image, &colors[(frame + i) % 4], 1,
                                             &rects[i]);
            }
        }
    }

    frame_manager_->UpdateSourceImage(image);
    const uint32_t new_width = pixman_image_get_width(image);
    const uint32_t new_height = pixman_image_get_height(image);
    SetDimensions(new_width, new_height);
    VLOG(2) << "updateSourceImage: " << *this << " to: " << image;
    if (old_width != new_width || old_height != new_height) {
        VLOG(2) << "Informing listeners of change from " << old_width << "x" << old_height << " to "
                << new_width << "x" << new_height << "\n";
        ResizeEventCallbackSource::FireEvent(ResizeEvent{.display_id = display_id_,
                                                         .previous_width = old_width,
                                                         .previous_height = old_height,
                                                         .width = new_width,
                                                         .height = new_height});
    }
}

absl::StatusOr<FrameInfo> PixmanDisplay::GetPixels(PixelFormat format, int new_width,
                                                   int new_height, ImageRotation rotation,
                                                   uint8_t* pixels, size_t* c_pixels) const {
    // NOTE: We expect newWidth and new_height to be safe, shearing *WILL* happen if the ratios
    // are not proper.
    const absl::MutexLock lock(pixman_mutex_);
    const absl::Time now = android::base::IClock::HostNow();
    auto pixman_fmt = PixmanFormat(format);
    auto bpp = PIXMAN_FORMAT_BPP(pixman_fmt);
    auto stride = ((static_cast<size_t>(new_width) * bpp + sizeof(uint32_t) * CHAR_BIT - 1) /
                   (sizeof(uint32_t) * CHAR_BIT)) *
                  sizeof(uint32_t);
    const size_t required_size = new_height * stride;

    if (required_size > *c_pixels) {
        auto old = *c_pixels;
        *c_pixels = required_size;
        return absl::FailedPreconditionError(
                absl::StrFormat("Buffer too small; need %u bytes, have %u", required_size, old));
    }

    auto* pixel = reinterpret_cast<uint32_t*>(pixels);
    // Create the destination image
    const PixmanImagePtr dst_img(pixman_image_create_bits(pixman_fmt, new_width, new_height, pixel,
                                                          static_cast<int>(stride)));

    absl::Status status = absl::OkStatus();
    Dimensions dims;
    int rot_channel = 0;

    frame_manager_->WithRenderableImage([&](::pixman_image_t* src_img) {
        if (!src_img) {
            status = absl::UnavailableError("No frame has been produced yet.");
            return;
        }

        dims = {.width = static_cast<uint32_t>(pixman_image_get_width(src_img)),
                .height = static_cast<uint32_t>(pixman_image_get_height(src_img))};

        if (kNoScaling) {
            if (format == PixelFormat::kPng) {
                auto width = pixman_image_get_width(src_img);
                auto height = pixman_image_get_height(src_img);
                auto* src_bits = pixman_image_get_data(src_img);
                memcpy(pixels, src_bits, static_cast<size_t>(width) * height * 4);
                rot_channel = 4;
            } else {
                rot_channel = 3;
                auto width = pixman_image_get_width(src_img);
                auto height = pixman_image_get_height(src_img);
                auto* src_bits = pixman_image_get_data(src_img);
                const size_t required_size2 = static_cast<size_t>(width) * height * 3;
                if (required_size2 > *c_pixels) {
                    auto old = *c_pixels;
                    *c_pixels = required_size2;
                    status = absl::FailedPreconditionError(absl::StrFormat(
                            "Buffer too small; need %u bytes, have %u", required_size2, old));
                    return;
                }
                Imaging::AbgrToRgbLe(reinterpret_cast<const uint32_t*>(src_bits), pixels,
                                     width * height);
            }
        } else {
            // Note: the source image dimensions do not have to match the current display
            // dimensions, since during boot we switch from the "qemu default screen (no display
            // present)" to the actual display size, which can happen at any time.

            // If we are rotated 90/270, the destination width fits the source height.
            const bool needs_dimension_swap = (rotation == ImageRotation::kRotation90 ||
                                               rotation == ImageRotation::kRotation270);
            const double scale_x = needs_dimension_swap
                                           ? static_cast<double>(dims.height) / new_width
                                           : static_cast<double>(dims.width) / new_width;
            const double scale_y = needs_dimension_swap
                                           ? static_cast<double>(dims.width) / new_height
                                           : static_cast<double>(dims.height) / new_height;

            VLOG(2) << "Source: " << dims.width << "x" << dims.height << ", dest: " << new_width
                    << "x" << new_height << ", rotation: " << static_cast<int>(rotation)
                    << ", scale_x: " << scale_x << ", scale_y: " << scale_y;
            // centering/translation logic.
            ::pixman_transform_t transform;
            pixman_transform_init_identity(&transform);

            if (rotation != ImageRotation::kRotation0) {
                // We want to rotate around the center.
                // The transform maps destination -> source.
                const double radians = static_cast<int>(rotation) * kPi / 180.0;

                // 1. Translate destination center to origin
                pixman_transform_translate(&transform, nullptr,
                                           pixman_double_to_fixed(-new_width / 2.0),    // move left
                                           pixman_double_to_fixed(-new_height / 2.0));  // move up

                // 2. Rotate (destination to source)
                // A clockwise rotation of the coordinate system (destination->source)
                // results in a counter-clockwise rotation of the image content.
                pixman_transform_rotate(&transform, nullptr,
                                        pixman_double_to_fixed(cos(radians)),   // cos(theta)
                                        pixman_double_to_fixed(sin(radians)));  // sin(theta)

                // 3. Translate back to source center
                pixman_transform_translate(&transform, nullptr,
                                           pixman_double_to_fixed(dims.width / 2.0),
                                           pixman_double_to_fixed(dims.height / 2.0));
            }

            if (!kNoScaling) {
                // Scaling logic.
                // Shift the whole image by -0.5 so we are looking at the center of each pixel
                // instead of the edge. This is important for scaling to be smooth.
                pixman_transform_translate(&transform, nullptr, pixman_double_to_fixed(-0.5),
                                           pixman_double_to_fixed(-0.5));
                // Perform the scaling.
                pixman_transform_scale(&transform, nullptr, pixman_double_to_fixed(scale_x),
                                       pixman_double_to_fixed(scale_y));
                // Move the image back to the original position (+0.5).
                pixman_transform_translate(&transform, nullptr, pixman_double_to_fixed(0.5),
                                           pixman_double_to_fixed(0.5));
                // Set the transform and filter on the source image for fast scaling.
                pixman_image_set_filter(src_img, PIXMAN_FILTER_NEAREST, nullptr, 0);
            }

            pixman_image_set_transform(src_img, &transform);

            pixman_image_composite(PIXMAN_OP_SRC, src_img, nullptr, dst_img.get(), 0, 0, 0, 0, 0, 0,
                                   new_width, new_height);

            // The buffer is now filled with the scaled and rotated image.
            // The size of the valid pixel data is the required size.
            *c_pixels = required_size;
            VLOG(2) << "GetPixels source {w:" << dims.width << " h:" << dims.height
                    << "}, dest {w:" << new_width << " h:" << new_height
                    << "} px_size=" << *c_pixels;
        }
    });

    if (!status.ok()) {
        return status;
    }

    if (kNoScaling && rotation != ImageRotation::kRotation0) {
        const int w = static_cast<int>(dims.width);
        const int h = static_cast<int>(dims.height);
        const int channels = rot_channel;
        const size_t buffer_size = static_cast<size_t>(w) * h * channels;

        const auto* src = pixels;
        std::vector<uint8_t> dst(buffer_size);
        ImageRotator::Rotate(src, dst.data(), w, h, channels, rotation);
        memcpy(pixels, dst.data(), buffer_size);
    }

    if (format == PixelFormat::kPng) {
        std::vector<uint8_t> png_buffer_vec;
        constexpr int kNChannels = 4;
        if (!write_png(kNChannels, new_width, new_height, pixels, png_buffer_vec)) {
            return absl::UnavailableError("Failed to create PNG screenshot!");
        }
        const size_t png_size = png_buffer_vec.size();
        memcpy(pixels, png_buffer_vec.data(), png_size);
        assert(png_size <= *c_pixels);
        *c_pixels = png_size;
    }

    const absl::MutexLock seqlock(seq_access_);

    VLOG(2) << "Image scaled in: " << (android::base::IClock::HostNow() - now);
    return seq_;
}

void PixmanDisplay::UpdateSurface(int x, int y, int width, int height) {
    VLOG(2) << "UpdateSurface " << *this << ", to: (" << x << ", " << y << "), (" << width << "x"
            << height << ")";

    frame_manager_->UpdateSurface();
    FrameReceived();
    if (ABSL_VLOG_IS_ON(2)) {
        fps_calculator_.AddFrame();
        VLOG_EVERY_N_SEC(2, 1) << "Qemu framerate: " << fps_calculator_.GetFps() << " fps";
    }
}

std::pair<int, int> PixmanDisplay::ResizeKeepAspectRatio(int desired_width, int desired_height) {
    auto fit = CalculateLogicalFit(desired_width, desired_height);
    if (fit.width == 0 || fit.height == 0) {
        return {0, 0};
    }

    // fast pass: no scaling
    // just return the original w and h, or swap them if
    // necessary (for 90 and 270 rotation)

    const Dimensions dims = GetDimensions();
    if (kNoScaling) {
        if (fit.swapped) {
            return {static_cast<int>(dims.height), static_cast<int>(dims.width)};
        }
        return {static_cast<int>(dims.width), static_cast<int>(dims.height)};
    }

    // The physical width of the destination buffer is always fit.width.
    // To ensure scaling is safe, we check the ratio between the source physical
    // stride and this destination physical width.
    const int source_physical_stride = static_cast<int>(fit.swapped ? dims.height : dims.width);
    int safe_width = fit.width;

    if (!IsScalingSafe(source_physical_stride, safe_width)) {
        for (int w_check = safe_width; w_check > 0; --w_check) {
            if (IsScalingSafe(source_physical_stride, w_check)) {
                safe_width = w_check;
                break;
            }
        }
    }

    // Logically oriented source dimensions.
    const int64_t s_width = fit.swapped ? dims.height : dims.width;
    const int64_t s_height = fit.swapped ? dims.width : dims.height;

    // Recalculate the logical height based on the safe logical width to
    // maintain the aspect ratio.
    const int safe_height = static_cast<int>((s_height * safe_width) / s_width);

    VLOG(2) << "Requested " << desired_width << "x" << desired_height << ", ideal " << fit.width
            << "x" << fit.height << ", snapped to safe " << safe_width << "x" << safe_height;

    return {safe_width, safe_height};
}

}  // namespace goldfish::display
