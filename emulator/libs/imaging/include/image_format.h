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

#ifndef GOLDFISH_IMAGE_FORMAT_H
#define GOLDFISH_IMAGE_FORMAT_H

#include <cstdint>

#ifdef __cplusplus
#include <ostream>

namespace goldfish::imaging {

/**
 * @enum ImageFormat
 * @brief Represents the supported image formats within the imaging library.
 *
 * This enum defines the internal representation for various image formats
 * used for frame generation, validation, and conversion.
 */
enum class ImageFormat : uint8_t {
    kNone = 0,
    kRgba8888 = 1,
    kYuV4203P = 2,
    kYuV420NV12 = 3,
};

inline std::ostream& operator<<(std::ostream& os, ImageFormat format) {
    switch (format) {
    case ImageFormat::kNone:
        return os << "kNone";
    case ImageFormat::kRgba8888:
        return os << "kRgba8888";
    case ImageFormat::kYuV4203P:
        return os << "kYuV4203P";
    case ImageFormat::kYuV420NV12:
        return os << "kYuV420NV12";
    default:
        return os << "Unknown";
    }
}

}  // namespace goldfish::imaging

using GOLDFISH_IMAGE_FORMAT = goldfish::imaging::ImageFormat;
// NOLINTBEGIN(readability-identifier-naming)
constexpr GOLDFISH_IMAGE_FORMAT GOLDFISH_IMAGE_FORMAT_NONE = GOLDFISH_IMAGE_FORMAT::kNone;
constexpr GOLDFISH_IMAGE_FORMAT GOLDFISH_IMAGE_FORMAT_RGBA_8888 = GOLDFISH_IMAGE_FORMAT::kRgba8888;
constexpr GOLDFISH_IMAGE_FORMAT GOLDFISH_IMAGE_FORMAT_YUV420_3P = GOLDFISH_IMAGE_FORMAT::kYuV4203P;
constexpr GOLDFISH_IMAGE_FORMAT GOLDFISH_IMAGE_FORMAT_YUV420_NV12 =
        GOLDFISH_IMAGE_FORMAT::kYuV420NV12;
// NOLINTEND(readability-identifier-naming)

#else

typedef enum GOLDFISH_IMAGE_FORMAT {
    GOLDFISH_IMAGE_FORMAT_NONE = 0,
    GOLDFISH_IMAGE_FORMAT_RGBA_8888 = 1,
    GOLDFISH_IMAGE_FORMAT_YUV420_3P = 2,    // 3 planes: Y, U and V
    GOLDFISH_IMAGE_FORMAT_YUV420_NV12 = 3,  // 2 planes: Y and UV (interleaved)
} GOLDFISH_IMAGE_FORMAT;

#endif  //  __cplusplus

#endif
