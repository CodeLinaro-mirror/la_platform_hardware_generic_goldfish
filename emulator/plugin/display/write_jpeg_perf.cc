// Copyright (C) 2026 The Android Open Source Project
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

#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "benchmark/benchmark.h"

#include "goldfish/display/write_jpeg.h"
#include "image_writer_test_utils.h"

extern "C" {
#include <jpeglib.h>
}

namespace goldfish::display {
namespace {

using test_utils::MakeSyntheticScreenshot;

constexpr unsigned int kFrameWidth = 1080;
constexpr unsigned int kFrameHeight = 1920;
constexpr unsigned int kRgbaChannels = 4;
constexpr unsigned int kRgbChannels = 3;

struct JpegTestErrorMgr {
    jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

struct JpegMemBuffer {
    unsigned char* data = nullptr;
    unsigned long size = 0;
};

void OnJpegTestErrorExit(j_common_ptr cinfo) {
    auto* err = reinterpret_cast<JpegTestErrorMgr*>(cinfo->err);
    longjmp(err->setjmp_buffer, 1);
}

template <typename WriteRowsFn>
bool EncodeWithJpegMemDest(unsigned int width, unsigned int height, int input_components,
                           J_COLOR_SPACE color_space, int quality, JpegMemBuffer* mem,
                           std::string* out_data, WriteRowsFn write_rows) {
    jpeg_compress_struct cinfo{};
    JpegTestErrorMgr jerr{};
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = OnJpegTestErrorExit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_compress(&cinfo);
        std::free(mem->data);
        mem->data = nullptr;
        return false;
    }

    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, &mem->data, &mem->size);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = input_components;
    cinfo.in_color_space = color_space;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    write_rows(&cinfo);

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    out_data->assign(reinterpret_cast<const char*>(mem->data), static_cast<size_t>(mem->size));
    std::free(mem->data);
    mem->data = nullptr;
    return true;
}

// Reference RGBA path prior to optimization: per-call compressor init, jpeg_mem_dest
// reallocations, default JDCT_ISLOW, and per-row RGB stripping.
bool WriteJpegLegacyUnoptimized(unsigned int width, unsigned int height, const void* pixels,
                                std::string* out_data, int quality) {
    const auto* byte_pixels = static_cast<const uint8_t*>(pixels);
    const size_t rgba_stride = static_cast<size_t>(width) * kRgbaChannels;
    std::vector<uint8_t> rgb_row(static_cast<size_t>(width) * kRgbChannels);
    JpegMemBuffer mem;
    return EncodeWithJpegMemDest(
            width, height, kRgbChannels, JCS_RGB, quality, &mem, out_data,
            [&](jpeg_compress_struct* cinfo) {
                while (cinfo->next_scanline < cinfo->image_height) {
                    const uint8_t* src =
                            byte_pixels + (static_cast<size_t>(cinfo->next_scanline) * rgba_stride);
                    for (unsigned int x = 0; x < width; ++x) {
                        rgb_row[static_cast<size_t>(x) * kRgbChannels + 0U] =
                                src[static_cast<size_t>(x) * kRgbaChannels + 0U];
                        rgb_row[static_cast<size_t>(x) * kRgbChannels + 1U] =
                                src[static_cast<size_t>(x) * kRgbaChannels + 1U];
                        rgb_row[static_cast<size_t>(x) * kRgbChannels + 2U] =
                                src[static_cast<size_t>(x) * kRgbaChannels + 2U];
                    }
                    JSAMPROW row_pointer = rgb_row.data();
                    jpeg_write_scanlines(cinfo, &row_pointer, 1);
                }
            });
}

// Reference with JCS_EXT_RGBA SIMD enabled, using per-call compressor init, jpeg_mem_dest,
// JDCT_ISLOW, and 1-row jpeg_write_scanlines.
bool WriteJpegSimdMemDestUnbatched(unsigned int width, unsigned int height, const void* pixels,
                                   std::string* out_data, int quality) {
    const auto* byte_pixels = static_cast<const uint8_t*>(pixels);
    const size_t stride = static_cast<size_t>(width) * kRgbaChannels;
    JpegMemBuffer mem;
    return EncodeWithJpegMemDest(
            width, height, kRgbaChannels, JCS_EXT_RGBA, quality, &mem, out_data,
            [&](jpeg_compress_struct* cinfo) {
                while (cinfo->next_scanline < cinfo->image_height) {
                    JSAMPROW row_pointer = const_cast<JSAMPROW>(
                            byte_pixels + (static_cast<size_t>(cinfo->next_scanline) * stride));
                    jpeg_write_scanlines(cinfo, &row_pointer, 1);
                }
            });
}

// Encodes a synthetic 1080x1920 RGBA screenshot once per iteration with @p encode.
template <typename EncodeFn>
void EncodeScreenshotPerIteration(benchmark::State& state, EncodeFn encode) {
    const std::vector<uint8_t> frame =
            MakeSyntheticScreenshot(kFrameWidth, kFrameHeight, kRgbaChannels);
    for (auto _ : state) {
        if (!encode(frame.data())) {
            state.SkipWithError("JPEG encoding failed");
            return;
        }
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(frame.size()));
}

void BmWriteJpegLegacyUnoptimized(benchmark::State& state) {
    EncodeScreenshotPerIteration(state, [](const uint8_t* pixels) {
        std::string out;
        const bool encoded = WriteJpegLegacyUnoptimized(kFrameWidth, kFrameHeight, pixels, &out,
                                                        kDefaultJpegQuality);
        benchmark::DoNotOptimize(out);
        return encoded;
    });
}

void BmWriteJpegSimdMemDestUnbatched(benchmark::State& state) {
    EncodeScreenshotPerIteration(state, [](const uint8_t* pixels) {
        std::string out;
        const bool encoded = WriteJpegSimdMemDestUnbatched(kFrameWidth, kFrameHeight, pixels, &out,
                                                           kDefaultJpegQuality);
        benchmark::DoNotOptimize(out);
        return encoded;
    });
}

void BmWriteJpeg(benchmark::State& state) {
    // Reused across frames so the benchmark includes WriteJpeg's output buffer reuse.
    std::string out;
    EncodeScreenshotPerIteration(state, [&out](const uint8_t* pixels) {
        const bool encoded = WriteJpeg(kRgbaChannels, kFrameWidth, kFrameHeight, pixels, &out,
                                       kDefaultJpegQuality);
        benchmark::DoNotOptimize(out);
        return encoded;
    });
}

BENCHMARK(BmWriteJpegLegacyUnoptimized)->Unit(benchmark::kMillisecond);
BENCHMARK(BmWriteJpegSimdMemDestUnbatched)->Unit(benchmark::kMillisecond);
BENCHMARK(BmWriteJpeg)->Unit(benchmark::kMillisecond);

}  // namespace
}  // namespace goldfish::display
