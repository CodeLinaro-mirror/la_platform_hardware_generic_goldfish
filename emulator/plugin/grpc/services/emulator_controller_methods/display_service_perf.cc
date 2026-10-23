// Copyright 2026 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0

/**
 * @file display_service_perf.cc
 * @brief Google Benchmark suite for DisplayServiceImpl screenshot buffer
 *        allocation and reuse across variable-size frames and RGBA scratch
 *        buffers.
 *
 * Benchmark results (`bazel run -c opt //emulator/plugin/grpc/services/emulator_controller_methods:display_service_perf`):
 * ----------------------------------------------------------------------------------------------------
 * Benchmark                                                  Time             CPU   Iterations
 * ----------------------------------------------------------------------------------------------------
 * BM_StandardAllocator_SetAllocatedBaseline/65536         1886 ns         1884 ns       371950  32.3936Gi/s
 * BM_StandardAllocator_SetAllocatedBaseline/262144        7243 ns         7242 ns        96985  33.6941Gi/s
 * BM_StandardAllocator_SetAllocatedBaseline/1048576      49417 ns        49401 ns        14063  19.7662Gi/s
 * BM_StandardAllocator_SetAllocatedBaseline/4194304     209256 ns       209200 ns         3329   18.672Gi/s
 * BM_StandardAllocator_SetAllocatedBaseline/8388608     450729 ns       450636 ns         1566  17.3366Gi/s
 * BM_StandardAllocator_ResizeReuse/65536                  15.9 ns         15.9 ns     44156692  3.75101Ti/s
 * BM_StandardAllocator_ResizeReuse/262144                 15.7 ns         15.7 ns     44419751  15.1743Ti/s
 * BM_StandardAllocator_ResizeReuse/1048576                12.3 ns         12.3 ns     56706716  77.3198Ti/s
 * BM_StandardAllocator_ResizeReuse/4194304                12.4 ns         12.4 ns     56631618   308.84Ti/s
 * BM_StandardAllocator_ResizeReuse/8388608                13.8 ns         13.8 ns     50842086  552.712Ti/s
 * BM_PngRgbaScratch_VectorZeroInitBaseline/921600        99205 ns        99184 ns         7164  8.65378Gi/s
 * BM_PngRgbaScratch_VectorZeroInitBaseline/3686400      524292 ns       524148 ns         1344   6.5499Gi/s
 * BM_PngRgbaScratch_VectorZeroInitBaseline/8294400     1136216 ns      1136038 ns          607   6.7966Gi/s
 * BM_PngRgbaScratch_PooledScratchBuffer/921600           79595 ns        79583 ns         8754   10.785Gi/s
 * BM_PngRgbaScratch_PooledScratchBuffer/3686400         342453 ns       342396 ns         2043  10.0267Gi/s
 * BM_PngRgbaScratch_PooledScratchBuffer/8294400         703784 ns       703667 ns         1002  10.9728Gi/s
 */

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "emulator_controller.pb.h"
#include "goldfish/display/cpu_read_pipeline.h"

namespace android::emulation::control {
namespace {

constexpr int64_t kMinFrameBytes = int64_t{64} << 10;
constexpr int64_t kMaxFrameBytes = int64_t{8} << 20;
constexpr int kRangeMultiplier = 4;
constexpr size_t kFrameSizeToggleDelta = 64U;
constexpr uint8_t kTestFillByte = 0xAB;
constexpr int64_t kRgba360pBytes = int64_t{640} * 360 * 4;
constexpr int64_t kRgba720pBytes = int64_t{1280} * 720 * 4;
constexpr int64_t kRgba1080pBytes = int64_t{1920} * 1080 * 4;

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
uint8_t* StandardAllocatorSetAllocatedBaseline(Image* img, size_t size) {
    if (img->image().size() != size) {
        img->set_allocated_image(new std::string(size, 0));
    }
    return reinterpret_cast<uint8_t*>(img->mutable_image()->data());
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
uint8_t* StandardAllocatorResizeReuse(Image* img, size_t size) {
    std::string* image = img->mutable_image();
    image->resize(size);
    return reinterpret_cast<uint8_t*>(image->data());
}

// NOLINTNEXTLINE(readability-identifier-naming)
void BM_StandardAllocator_SetAllocatedBaseline(benchmark::State& state) {
    const auto base_size = static_cast<size_t>(state.range(0));
    Image reply;
    size_t toggle = 0;

    for (auto _ : state) {
        // Variable-size frames (such as compressed PNG frames in streamScreenshot)
        // vary slightly in size on every frame.
        const size_t frame_size = base_size - (toggle & kFrameSizeToggleDelta);
        toggle += kFrameSizeToggleDelta;
        uint8_t* dst = StandardAllocatorSetAllocatedBaseline(&reply, frame_size);
        dst[0] = static_cast<uint8_t>(toggle);
        dst[frame_size - 1] = static_cast<uint8_t>(toggle);
        benchmark::DoNotOptimize(dst);
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(base_size));
}

// NOLINTNEXTLINE(readability-identifier-naming)
void BM_StandardAllocator_ResizeReuse(benchmark::State& state) {
    const auto base_size = static_cast<size_t>(state.range(0));
    Image reply;
    size_t toggle = 0;

    for (auto _ : state) {
        const size_t frame_size = base_size - (toggle & kFrameSizeToggleDelta);
        toggle += kFrameSizeToggleDelta;
        uint8_t* dst = StandardAllocatorResizeReuse(&reply, frame_size);
        dst[0] = static_cast<uint8_t>(toggle);
        dst[frame_size - 1] = static_cast<uint8_t>(toggle);
        benchmark::DoNotOptimize(dst);
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(base_size));
}

// NOLINTNEXTLINE(readability-identifier-naming)
void BM_PngRgbaScratch_VectorZeroInitBaseline(benchmark::State& state) {
    const auto total_bytes = static_cast<size_t>(state.range(0));
    const std::vector<uint8_t> source(total_bytes, kTestFillByte);

    for (auto _ : state) {
        std::vector<uint8_t> rgba;
        rgba.resize(total_bytes);
        std::memcpy(rgba.data(), source.data(), total_bytes);
        benchmark::DoNotOptimize(rgba.data());
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(total_bytes));
}

// NOLINTNEXTLINE(readability-identifier-naming)
void BM_PngRgbaScratch_PooledScratchBuffer(benchmark::State& state) {
    const auto total_bytes = static_cast<size_t>(state.range(0));
    const std::vector<uint8_t> source(total_bytes, kTestFillByte);

    for (auto _ : state) {
        ::goldfish::display::ScratchBuffer rgba =
                ::goldfish::display::AcquireScratchBuffer(total_bytes);
        std::memcpy(rgba.data(), source.data(), total_bytes);
        benchmark::DoNotOptimize(rgba.data());
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(total_bytes));
}

BENCHMARK(BM_StandardAllocator_SetAllocatedBaseline)
        ->RangeMultiplier(kRangeMultiplier)
        ->Range(kMinFrameBytes, kMaxFrameBytes);
BENCHMARK(BM_StandardAllocator_ResizeReuse)
        ->RangeMultiplier(kRangeMultiplier)
        ->Range(kMinFrameBytes, kMaxFrameBytes);
BENCHMARK(BM_PngRgbaScratch_VectorZeroInitBaseline)
        ->Arg(kRgba360pBytes)
        ->Arg(kRgba720pBytes)
        ->Arg(kRgba1080pBytes);
BENCHMARK(BM_PngRgbaScratch_PooledScratchBuffer)
        ->Arg(kRgba360pBytes)
        ->Arg(kRgba720pBytes)
        ->Arg(kRgba1080pBytes);

}  // namespace
}  // namespace android::emulation::control

BENCHMARK_MAIN();
