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

#include <benchmark/benchmark.h>
#include <libyuv.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "raw_image_sources/fourcc_utils.h"

namespace android {
namespace ver {
namespace {

constexpr size_t kBytesPerRgbaPixel = 4U;

std::vector<uint8_t> MakeSyntheticSourceFrame(uint32_t fourcc, int width, int height) {
    const size_t y_size = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (fourcc == V4L2_PIX_FMT_YUYV) {
        std::vector<uint8_t> frame(y_size * 2U);
        for (size_t i = 0; i < frame.size(); i += 4U) {
            frame[i + 0] = static_cast<uint8_t>((i / 4U) & 0xFFU);
            frame[i + 1] = 96U;
            frame[i + 2] = static_cast<uint8_t>(((i / 4U) + 17U) & 0xFFU);
            frame[i + 3] = 160U;
        }
        return frame;
    }

    const size_t uv_bytes =
            static_cast<size_t>((width + 1) / 2) * static_cast<size_t>((height + 1) / 2) * 2U;
    std::vector<uint8_t> frame(y_size + uv_bytes);
    for (size_t i = 0; i < y_size; ++i) {
        frame[i] = static_cast<uint8_t>(16U + (i % 219U));
    }
    for (size_t i = y_size; i < frame.size(); ++i) {
        frame[i] = static_cast<uint8_t>((i & 1U) ? 156U : 104U);
    }
    return frame;
}

int LegacyConvertBufferWithI420Staging(RawImageBufferViewFourCC& buffer,
                                       std::vector<uint8_t>& conversion_storage,
                                       std::vector<uint8_t>& staging_buffer) {
    const int width = buffer.width;
    const int height = buffer.height;
    const size_t rgba_bytes =
            static_cast<size_t>(width) * static_cast<size_t>(height) * kBytesPerRgbaPixel;
    if (conversion_storage.size() < rgba_bytes) {
        conversion_storage.resize(rgba_bytes);
    }

    const int y_stride = (width + 15) & ~15;
    const int u_stride = (y_stride / 2 + 15) & ~15;
    const int v_stride = u_stride;
    const size_t y_size = static_cast<size_t>(y_stride) * static_cast<size_t>(height);
    const size_t u_size = static_cast<size_t>(u_stride) * static_cast<size_t>(height / 2);
    const size_t required_staging_size = y_size + u_size * 2U;
    if (staging_buffer.size() < required_staging_size) {
        staging_buffer.resize(required_staging_size);
    }

    uint8_t* y_staging = staging_buffer.data();
    uint8_t* u_staging = y_staging + y_size;
    uint8_t* v_staging = u_staging + u_size;
    int res = libyuv::ConvertToI420(buffer.buffer, buffer.buffer_size, y_staging, y_stride,
                                    u_staging, u_stride, v_staging, v_stride, 0, 0, width, height,
                                    width, height, libyuv::kRotate0, buffer.pixel_format);
    if (res != 0) {
        return -1;
    }
    res = libyuv::ConvertFromI420(y_staging, y_stride, u_staging, u_stride, v_staging, v_stride,
                                  conversion_storage.data(), 0, width, height, libyuv::FOURCC_ABGR);
    if (res != 0) {
        return res;
    }

    buffer.buffer_size = rgba_bytes;
    buffer.pixel_format = V4L2_PIX_FMT_RGB32;
    buffer.buffer = conversion_storage.data();
    return 0;
}

void AddFourccResolutionArgs(benchmark::internal::Benchmark* b) {
    for (uint32_t fourcc : {V4L2_PIX_FMT_NV12, V4L2_PIX_FMT_YUV420, V4L2_PIX_FMT_YUYV}) {
        b->Args({static_cast<int64_t>(fourcc), 1280, 720});
        b->Args({static_cast<int64_t>(fourcc), 1920, 1080});
        b->Args({static_cast<int64_t>(fourcc), 3840, 2160});
    }
}

void BM_ConvertFourccBaseline_TwoPassI420(benchmark::State& state) {
    const uint32_t fourcc = static_cast<uint32_t>(state.range(0));
    const int width = static_cast<int>(state.range(1));
    const int height = static_cast<int>(state.range(2));
    const std::vector<uint8_t> src_frame = MakeSyntheticSourceFrame(fourcc, width, height);
    std::vector<uint8_t> conversion_storage;
    std::vector<uint8_t> staging_buffer;

    state.SetLabel(FourccToString(fourcc));
    for (auto _ : state) {
        RawImageBufferViewFourCC view = {src_frame.data(), src_frame.size(), fourcc, width, height};
        const int res =
                LegacyConvertBufferWithI420Staging(view, conversion_storage, staging_buffer);
        benchmark::DoNotOptimize(res);
        benchmark::DoNotOptimize(conversion_storage.data());
        benchmark::ClobberMemory();
    }

    state.counters["staging_bytes"] = static_cast<double>(staging_buffer.size());
    state.counters["conversion_bytes"] = static_cast<double>(conversion_storage.size());
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(src_frame.size() + conversion_storage.size()));
}
BENCHMARK(BM_ConvertFourccBaseline_TwoPassI420)
        ->Apply(AddFourccResolutionArgs)
        ->Unit(benchmark::kMicrosecond);

void BM_ConvertFourccDirect_ConvertBufferToRGB32(benchmark::State& state) {
    const uint32_t fourcc = static_cast<uint32_t>(state.range(0));
    const int width = static_cast<int>(state.range(1));
    const int height = static_cast<int>(state.range(2));
    const std::vector<uint8_t> src_frame = MakeSyntheticSourceFrame(fourcc, width, height);
    std::vector<uint8_t> conversion_storage;

    state.SetLabel(FourccToString(fourcc));
    for (auto _ : state) {
        RawImageBufferViewFourCC view = {src_frame.data(), src_frame.size(), fourcc, width, height};
        const int res = ConvertBufferToRGB32(view, conversion_storage);
        benchmark::DoNotOptimize(res);
        benchmark::DoNotOptimize(conversion_storage.data());
        benchmark::ClobberMemory();
    }

    state.counters["staging_bytes"] = 0.0;
    state.counters["conversion_bytes"] = static_cast<double>(conversion_storage.size());
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(src_frame.size() + conversion_storage.size()));
}
BENCHMARK(BM_ConvertFourccDirect_ConvertBufferToRGB32)
        ->Apply(AddFourccResolutionArgs)
        ->Unit(benchmark::kMicrosecond);

}  // namespace
}  // namespace ver
}  // namespace android

BENCHMARK_MAIN();
