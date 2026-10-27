// Copyright 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "fourcc_utils.h"

#include <libyuv.h>

#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>

#include "JpegUtils.h"

namespace android {
namespace ver {
namespace {

constexpr size_t kBytesPerRgb24Pixel = 3U;
constexpr size_t kBytesPerRgbaPixel = 4U;

uint32_t NormalizeFourcc(uint32_t fourcc) {
    switch (fourcc) {
    case V4L2_PIX_FMT_RGB32:
        return libyuv::FOURCC_ABGR;
    case V4L2_PIX_FMT_BGR32:
        return libyuv::FOURCC_ARGB;
    case V4L2_PIX_FMT_YUY2:
    case V4L2_PIX_FMT_YUNV:
    case V4L2_PIX_FMT_V422:
        return libyuv::FOURCC_YUY2;
    default:
        return libyuv::CanonicalFourCC(fourcc);
    }
}

std::optional<size_t> MinUncompressedByteSize(uint32_t canonical_fourcc, int width, int height) {
    const size_t w = static_cast<size_t>(width);
    const size_t h = static_cast<size_t>(height);
    const size_t half_w = (w + 1U) / 2U;
    const size_t half_h = (h + 1U) / 2U;
    const size_t aligned_w = half_w * 2U;

    switch (canonical_fourcc) {
    case libyuv::FOURCC_ABGR:
    case libyuv::FOURCC_ARGB:
    case libyuv::FOURCC_BGRA:
    case libyuv::FOURCC_RGBA:
    case libyuv::FOURCC_AR30:
    case libyuv::FOURCC_AB30:
        return w * h * kBytesPerRgbaPixel;
    case libyuv::FOURCC_RAW:
    case libyuv::FOURCC_24BG:
    case libyuv::FOURCC_I444:
    case libyuv::FOURCC_YV24:
    case libyuv::FOURCC_J444:
    case libyuv::FOURCC_H444:
    case libyuv::FOURCC_U444:
        return w * h * kBytesPerRgb24Pixel;
    case libyuv::FOURCC_YUY2:
    case libyuv::FOURCC_UYVY:
        return aligned_w * h * 2U;
    case libyuv::FOURCC_RGBP:
    case libyuv::FOURCC_RGBO:
    case libyuv::FOURCC_R444:
        return w * h * 2U;
    case libyuv::FOURCC_I400:
    case libyuv::FOURCC_J400:
        return w * h;
    case libyuv::FOURCC_NV12:
    case libyuv::FOURCC_NV21:
    case libyuv::FOURCC_I420:
    case libyuv::FOURCC_YV12:
    case libyuv::FOURCC_J420:
    case libyuv::FOURCC_H420:
    case libyuv::FOURCC_U420:
        return w * h + 2U * half_w * half_h;
    case libyuv::FOURCC_I422:
    case libyuv::FOURCC_YV16:
    case libyuv::FOURCC_J422:
    case libyuv::FOURCC_H422:
    case libyuv::FOURCC_U422:
        return w * h + 2U * half_w * h;
    default:
        return std::nullopt;
    }
}

int ConvertUncompressedToAbgr(const uint8_t* src, size_t src_size, uint32_t canonical_fourcc,
                              int width, int height, uint8_t* dst) {
    const int dst_stride = width * static_cast<int>(kBytesPerRgbaPixel);
    const size_t y_size = static_cast<size_t>(width) * static_cast<size_t>(height);
    const int half_width = (width + 1) / 2;
    const int half_height = (height + 1) / 2;
    const size_t uv_plane_size = static_cast<size_t>(half_width) * static_cast<size_t>(half_height);

    switch (canonical_fourcc) {
    case libyuv::FOURCC_ABGR:
        if (dst != src) {
            std::memcpy(dst, src, y_size * kBytesPerRgbaPixel);
        }
        return 0;
    case libyuv::FOURCC_ARGB:
        return libyuv::ARGBToABGR(src, dst_stride, dst, dst_stride, width, height);
    case libyuv::FOURCC_I420:
    case libyuv::FOURCC_YV12: {
        const uint8_t* plane_a = src + y_size;
        const uint8_t* plane_b = plane_a + uv_plane_size;
        const uint8_t* src_u = (canonical_fourcc == libyuv::FOURCC_YV12) ? plane_b : plane_a;
        const uint8_t* src_v = (canonical_fourcc == libyuv::FOURCC_YV12) ? plane_a : plane_b;
        return libyuv::I420ToABGR(src, width, src_u, half_width, src_v, half_width, dst, dst_stride,
                                  width, height);
    }
    case libyuv::FOURCC_NV12:
        return libyuv::NV12ToABGR(src, width, src + y_size, half_width * 2, dst, dst_stride, width,
                                  height);
    case libyuv::FOURCC_NV21:
        return libyuv::NV21ToABGR(src, width, src + y_size, half_width * 2, dst, dst_stride, width,
                                  height);
    case libyuv::FOURCC_I422:
    case libyuv::FOURCC_YV16: {
        const size_t chroma_422_size =
                static_cast<size_t>(half_width) * static_cast<size_t>(height);
        const uint8_t* plane_a = src + y_size;
        const uint8_t* plane_b = plane_a + chroma_422_size;
        const uint8_t* src_u = (canonical_fourcc == libyuv::FOURCC_YV16) ? plane_b : plane_a;
        const uint8_t* src_v = (canonical_fourcc == libyuv::FOURCC_YV16) ? plane_a : plane_b;
        return libyuv::I422ToABGR(src, width, src_u, half_width, src_v, half_width, dst, dst_stride,
                                  width, height);
    }
    case libyuv::FOURCC_I444:
    case libyuv::FOURCC_YV24: {
        const uint8_t* plane_a = src + y_size;
        const uint8_t* plane_b = plane_a + y_size;
        const uint8_t* src_u = (canonical_fourcc == libyuv::FOURCC_YV24) ? plane_b : plane_a;
        const uint8_t* src_v = (canonical_fourcc == libyuv::FOURCC_YV24) ? plane_a : plane_b;
        return libyuv::I444ToABGR(src, width, src_u, width, src_v, width, dst, dst_stride, width,
                                  height);
    }
    case libyuv::FOURCC_RAW:
        // libyuv::RGB24ToARGB copies [s0, s1, s2] -> [s0, s1, s2, 255],
        // turning RGB24 [R, G, B] directly into FOURCC_ABGR [R, G, B, 255].
        return libyuv::RGB24ToARGB(src, width * static_cast<int>(kBytesPerRgb24Pixel), dst,
                                   dst_stride, width, height);
    case libyuv::FOURCC_24BG:
        // libyuv::RAWToARGB swaps [s0, s1, s2] -> [s2, s1, s0, 255],
        // turning BGR24 [B, G, R] directly into FOURCC_ABGR [R, G, B, 255].
        return libyuv::RAWToARGB(src, width * static_cast<int>(kBytesPerRgb24Pixel), dst,
                                 dst_stride, width, height);
    default: {
        const int res = libyuv::ConvertToARGB(src, src_size, dst, dst_stride, 0, 0, width, height,
                                              width, height, libyuv::kRotate0, canonical_fourcc);
        if (res != 0) {
            return res;
        }
        return libyuv::ARGBToABGR(dst, dst_stride, dst, dst_stride, width, height);
    }
    }
}

}  // namespace

std::string FourccToString(uint32_t fourcc) {
    char buf[5];
    buf[0] = static_cast<char>(fourcc & 0xFFU);
    buf[1] = static_cast<char>((fourcc >> 8) & 0xFFU);
    buf[2] = static_cast<char>((fourcc >> 16) & 0xFFU);
    buf[3] = static_cast<char>((fourcc >> 24) & 0xFFU);
    buf[4] = '\0';
    return std::string(buf);
}

VerImageFormat FourccToVerImageFormat(uint32_t fourcc) {
    if (fourcc == V4L2_PIX_FMT_RGB32) {
        return VerImageFormat::RGBA8;
    }
    return VerImageFormat::UNKNOWN;
}

uint32_t VerImageFormatToFourcc(VerImageFormat ver_image_format) {
    if (ver_image_format == VerImageFormat::RGBA8) {
        return V4L2_PIX_FMT_RGB32;
    }
    return 0;
}

RawImageBufferView RawImageBufferViewFourCCBridge(RawImageBufferViewFourCC* fourcc) {
    return {fourcc->buffer, fourcc->buffer_size, FourccToVerImageFormat(fourcc->pixel_format),
            fourcc->width, fourcc->height};
}

int ConvertBufferToRGB32(RawImageBufferViewFourCC& buffer,
                         std::vector<uint8_t>& conversion_storage) {
    if (!buffer.buffer || buffer.buffer_size == 0) {
        return -1;
    }

    if (buffer.pixel_format == V4L2_PIX_FMT_MJPEG || buffer.pixel_format == V4L2_PIX_FMT_JPEG) {
        JpegImageInfo info;
        JpegDecompressOptions options;
        options.color_space = JCS_RGBA_8888;
        options.align_row_stride = false;
        if (!JpegUtils::DecompressFromMemory(buffer.buffer, buffer.buffer_size, conversion_storage,
                                             &info, options)) {
            return -1;
        }
        buffer.width = info.width;
        buffer.height = info.height;
        buffer.buffer_size = info.total_size;
        buffer.pixel_format = V4L2_PIX_FMT_RGB32;
        buffer.buffer = conversion_storage.data();
        return 0;
    }

    const int width = buffer.width;
    const int height = buffer.height;
    if (width <= 0 || height <= 0 ||
        static_cast<size_t>(width) >
                static_cast<size_t>(std::numeric_limits<int>::max()) / kBytesPerRgbaPixel) {
        return -1;
    }

    const size_t row_rgba_bytes = static_cast<size_t>(width) * kBytesPerRgbaPixel;
    if (static_cast<size_t>(height) > std::numeric_limits<size_t>::max() / row_rgba_bytes) {
        return -1;
    }
    const size_t rgba_size = row_rgba_bytes * static_cast<size_t>(height);

    const uint32_t canonical_fourcc = NormalizeFourcc(buffer.pixel_format);
    const std::optional<size_t> min_input_bytes =
            MinUncompressedByteSize(canonical_fourcc, width, height);
    if (!min_input_bytes.has_value() || buffer.buffer_size < *min_input_bytes) {
        return -1;
    }

    if (conversion_storage.size() < rgba_size) {
        conversion_storage.resize(rgba_size);
    }

    const int res = ConvertUncompressedToAbgr(buffer.buffer, buffer.buffer_size, canonical_fourcc,
                                              width, height, conversion_storage.data());
    if (res != 0) {
        return res;
    }

    buffer.buffer_size = rgba_size;
    buffer.pixel_format = V4L2_PIX_FMT_RGB32;
    buffer.buffer = conversion_storage.data();
    return 0;
}

}  // namespace ver
}  // namespace android
