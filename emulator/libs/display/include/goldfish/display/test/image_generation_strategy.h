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

/**
 * @file ImageGenerationStrategy.h
 * @brief Defines a set of diagnostic classes for generating and validating
 *        image patterns.
 *
 * These classes are primarily used in testing environments to diagnose and
 * debug image scaling and rendering issues, particularly those related to the
 * pixman library. By generating a known pattern and then validating it after
 * a transformation (like scaling), we can detect visual artifacts such as
 * shearing or color distortion.
 */
#pragma once
extern "C" {
#include "pixman.h"
}

#include <cmath>
#include <cstdint>
#include <vector>

namespace goldfish::display::test {

/**
 * @brief Helper function to get a deterministic color based on a frame number.
 *
 * This function cycles through kRed, kGreen, and Blue, allowing tests to use
 * different colors for different frames.
 * @param frame The frame number.
 * @return A 32-bit color value in ARGB format (e.g., 0xffff0000 for red).
 */
static inline uint32_t GetFrameColor(int frame) {
    switch (frame % 3) {
    case 0:
        return 0xffff0000;  // kRed
    case 1:
        return 0xff00ff00;  // kGreen
    case 2:
    default:
        return 0xff0000ff;  // kBlue
    }
}

//------------------------------------------------------------------------------
// Abstract Base Class
//------------------------------------------------------------------------------

/**
 * @class ImageGenerationStrategy
 * @brief An abstract base class for defining image generation and validation
 *        logic.
 *
 * This class provides an interface for strategies that can fill a pixman image
 * with a specific, reproducible pattern and later verify if an image conforms
 * to that pattern. It is a key component of the diagnostic toolkit for testing
 * image transformations.
 */
class ImageGenerationStrategy {
  public:
    virtual ~ImageGenerationStrategy() = default;

    /**
     * @brief Fills the target image with a pattern based on the frame number.
     * @param target_image The pixman image to draw onto. The strategy does not
     *                     take ownership of this pointer.
     * @param frame The current frame number, used to vary the pattern (e.g.,
     *              by changing colors).
     */
    virtual void Generate(::pixman_image_t* target_image, int frame) = 0;

    /**
     * @brief Validates if the given image matches the pattern this strategy would
     *        generate for the specified frame number.
     * @param image The pixman image to validate.
     * @param frame The frame number to check against.
     * @return true if the image matches the expected pattern, false otherwise.
     */
    virtual bool IsGeneratedBy(::pixman_image_t* image, int frame) = 0;
};

//------------------------------------------------------------------------------
// Concrete Strategy 1: FillColorStrategy
//------------------------------------------------------------------------------

/**
 * @class FillColorStrategy
 * @brief A concrete strategy that fills an image with a solid color.
 *
 * The color is determined by the frame number, cycling through red, green,
 * and blue.
 */
class FillColorStrategy : public ImageGenerationStrategy {
  public:
    void Generate(::pixman_image_t* target_image, int frame) override {
        const int width = pixman_image_get_width(target_image);
        const int height = pixman_image_get_height(target_image);
        const uint32_t color = GetFrameColor(frame);

        const pixman_color_t fill_color = {static_cast<uint16_t>(((color >> 16) & 0xff) * 0x101),
                                           static_cast<uint16_t>(((color >> 8) & 0xff) * 0x101),
                                           static_cast<uint16_t>(((color >> 0) & 0xff) * 0x101),
                                           static_cast<uint16_t>(((color >> 24) & 0xff) * 0x101)};

        const pixman_rectangle16_t rect = {0, 0, static_cast<uint16_t>(width),
                                           static_cast<uint16_t>(height)};
        pixman_image_fill_rectangles(PIXMAN_OP_SRC, target_image, &fill_color, 1, &rect);
    }

    bool IsGeneratedBy(::pixman_image_t* image, int frame) override {
        const int width = pixman_image_get_width(image);
        const int height = pixman_image_get_height(image);
        const uint32_t* data = pixman_image_get_data(image);
        const uint32_t expected_color = GetFrameColor(frame);

        for (int i = 0; i < width * height; ++i) {
            if (data[i] != expected_color) {
                return false;
            }
        }
        return true;
    }
};

//------------------------------------------------------------------------------
// Concrete Strategy 2: ChessboardStrategy
//------------------------------------------------------------------------------

/**
 * @class ChessboardStrategy
 * @brief A concrete strategy that generates an 8x8 chessboard pattern.
 *
 * This strategy is particularly useful for detecting scaling artifacts like
 * shearing, where straight lines become distorted. The validation logic is
 * robust against minor color variations by checking the average luminance of
 * expected light and dark bands rather than exact pixel colors.
 */
class ChessboardStrategy : public ImageGenerationStrategy {
  public:
    void Generate(::pixman_image_t* target_image, int /*frame*/) override {
        const int width = pixman_image_get_width(target_image);
        const int height = pixman_image_get_height(target_image);
        const int square_size_w = width / 8;
        const int square_size_h = height / 8;

        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const uint32_t color_val = ((x % 2) == (y % 2)) ? kColorWhite : kColorDarkGray;
                const pixman_color_t fill_color = {
                    static_cast<uint16_t>(((color_val >> 16) & 0xff) * 0x101),
                    static_cast<uint16_t>(((color_val >> 8) & 0xff) * 0x101),
                    static_cast<uint16_t>(((color_val >> 0) & 0xff) * 0x101),
                    static_cast<uint16_t>(((color_val >> 24) & 0xff) * 0x101)};

                const pixman_rectangle16_t rect = {static_cast<int16_t>(x * square_size_w),
                                                   static_cast<int16_t>(y * square_size_h),
                                                   static_cast<uint16_t>(square_size_w),
                                                   static_cast<uint16_t>(square_size_h)};
                pixman_image_fill_rectangles(PIXMAN_OP_SRC, target_image, &fill_color, 1, &rect);
            }
        }
    }

    bool IsGeneratedBy(::pixman_image_t* image, int /*frame*/) override {
        const int width = pixman_image_get_width(image);
        const int height = pixman_image_get_height(image);
        const uint32_t* data = pixman_image_get_data(image);
        if (!data) return false;
        const int stride = static_cast<int>(pixman_image_get_stride(image) / sizeof(uint32_t));

        auto get_luminance = [](uint32_t color) {
            const uint8_t r = (color >> 16) & 0xff;
            const uint8_t g = (color >> 8) & 0xff;
            const uint8_t b = (color >> 0) & 0xff;
            return ((0.299 * r) + (0.587 * g) + (0.114 * b));
        };

        const double k_white_luminance = get_luminance(kColorWhite);
        const double k_gray_luminance = get_luminance(kColorDarkGray);
        const double k_luminance_threshold = (k_white_luminance + k_gray_luminance) / 2.0;

        // Validate Vertical Bands
        std::vector<double> col_avg_lum(8, 0.0);
        const int band_width = width / 8;
        if (band_width == 0) return false;

        for (int band = 0; band < 8; ++band) {
            double total_lum = 0;
            int pixel_count = 0;
            const int start_col = band * band_width;
            const int end_col = (band == 7) ? width : (band + 1) * band_width;
            for (int r = 0; r < height; ++r) {
                for (int c = start_col; c < end_col; ++c) {
                    total_lum += get_luminance(data[(static_cast<size_t>(r) * stride) + c]);
                    pixel_count++;
                }
            }
            if (pixel_count > 0) col_avg_lum[band] = total_lum / pixel_count;
        }

        for (int i = 0; i < 8; ++i) {
            if ((i % 2) == 0) {  // Even bands should be light
                if (col_avg_lum[i] < k_luminance_threshold) return false;
            } else {  // Odd bands should be dark
                if (col_avg_lum[i] > k_luminance_threshold) return false;
            }
        }

        // Validate Horizontal Bands
        std::vector<double> row_avg_lum(8, 0.0);
        const int band_height = height / 8;
        if (band_height == 0) return false;

        for (int band = 0; band < 8; ++band) {
            double total_lum = 0;
            int pixel_count = 0;
            const int start_row = band * band_height;
            const int end_row = (band == 7) ? height : (band + 1) * band_height;
            for (int r = start_row; r < end_row; ++r) {
                for (int c = 0; c < width; ++c) {
                    total_lum += get_luminance(data[(static_cast<size_t>(r) * stride) + c]);
                    pixel_count++;
                }
            }
            if (pixel_count > 0) row_avg_lum[band] = total_lum / pixel_count;
        }

        for (int i = 0; i < 8; ++i) {
            if ((i % 2) == 0) {  // Even bands should be light
                if (row_avg_lum[i] < k_luminance_threshold) return false;
            } else {  // Odd bands should be dark
                if (row_avg_lum[i] > k_luminance_threshold) return false;
            }
        }

        return true;
    }

  private:
    static constexpr uint32_t kColorWhite = 0xffffffff;
    static constexpr uint32_t kColorDarkGray = 0xff303030;
};

//------------------------------------------------------------------------------
// Concrete Strategy 3: LinearGradientStrategy
//------------------------------------------------------------------------------

/**
 * @class LinearGradientStrategy
 * @brief A concrete strategy that generates a horizontal linear gradient.
 *
 * The gradient transitions from black on the left to a frame-dependent color
 * (red, green, or blue) on the right. This is useful for detecting color
 * blending and interpolation issues.
 */
class LinearGradientStrategy : public ImageGenerationStrategy {
  public:
    void Generate(::pixman_image_t* target_image, int frame) override {
        const int width = pixman_image_get_width(target_image);
        const int height = pixman_image_get_height(target_image);
        uint32_t* data = pixman_image_get_data(target_image);
        const int stride =
                static_cast<int>(pixman_image_get_stride(target_image) / sizeof(uint32_t));

        const uint32_t end_color = GetFrameColor(frame);
        const uint8_t r_end = (end_color >> 16) & 0xff;
        const uint8_t g_end = (end_color >> 8) & 0xff;
        const uint8_t b_end = (end_color >> 0) & 0xff;

        for (int x = 0; x < width; ++x) {
            const double ratio = static_cast<double>(x) / (width - 1);
            const auto r = static_cast<uint8_t>(ratio * r_end);
            const auto g = static_cast<uint8_t>(ratio * g_end);
            const auto b = static_cast<uint8_t>(ratio * b_end);
            const uint32_t pixel_color = (0xff << 24) | (r << 16) | (g << 8) | b;
            for (int y = 0; y < height; ++y) {
                data[(static_cast<size_t>(y) * stride) + x] = pixel_color;
            }
        }
    }

    bool IsGeneratedBy(::pixman_image_t* image, int frame) override {
        const int width = pixman_image_get_width(image);
        const int height = pixman_image_get_height(image);
        const uint32_t* data = pixman_image_get_data(image);
        const int stride = static_cast<int>(pixman_image_get_stride(image) / sizeof(uint32_t));

        const uint32_t end_color = GetFrameColor(frame);
        const uint8_t r_end = (end_color >> 16) & 0xff;
        const uint8_t g_end = (end_color >> 8) & 0xff;
        const uint8_t b_end = (end_color >> 0) & 0xff;

        for (int x = 0; x < width; ++x) {
            const double ratio = static_cast<double>(x) / (width - 1);
            const auto r = static_cast<uint8_t>(ratio * r_end);
            const auto g = static_cast<uint8_t>(ratio * g_end);
            const auto b = static_cast<uint8_t>(ratio * b_end);
            const uint32_t expected_color = (0xff << 24) | (r << 16) | (g << 8) | b;

            for (int y = 0; y < height; ++y) {
                if (data[(static_cast<size_t>(y) * stride) + x] != expected_color) {
                    return false;
                }
            }
        }
        return true;
    }
};

}  // namespace goldfish::display::test
