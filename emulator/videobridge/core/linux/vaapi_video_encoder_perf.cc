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

/**
 * @file vaapi_video_encoder_perf.cc
 * @brief Google Benchmark suite comparing VA-API coded bitstream assembly memory usage and
 *        throughput between the previous per-frame std::vector pipeline and capacity-retained
 *        scratch buffer assembly with WebRTC H.264 NALU utilities.
 */

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "absl/types/span.h"

#include "core/linux/test/vaapi_bitstream_test_utils.h"
#include "core/linux/va_api_types.h"
#include "core/linux/vaapi_video_encoder.h"

VAAPI_DEFINE_HEAP_TRACKER_NEW_DELETE()

namespace goldfish::videobridge {
namespace {

constexpr size_t kSingleDeltaBytes = 16ULL * 1024ULL;
constexpr size_t kMultiSegmentCount = 4U;
constexpr size_t kMultiDeltaSegmentBytes = 16ULL * 1024ULL;
constexpr size_t kMultiIdrSegmentBytes = 64ULL * 1024ULL;
constexpr uint8_t kSliceFillByte = 0x5AU;

struct SegmentChainFixture {
    std::vector<std::vector<uint8_t>> chunks;
    std::vector<VACodedBufferSegment> nodes;
    std::vector<uint8_t> sps_nalu;
    std::vector<uint8_t> pps_nalu;
    size_t total_input_bytes = 0U;
};

SegmentChainFixture MakeDeltaFixture(size_t num_segments, size_t bytes_per_segment) {
    SegmentChainFixture fixture;
    fixture.sps_nalu = {0x00U, 0x00U, 0x00U, 0x01U, 0x67U, 0x64U, 0x00U, 0x29U};
    fixture.pps_nalu = {0x00U, 0x00U, 0x00U, 0x01U, 0x68U, 0xEEU, 0x3CU, 0x80U};
    fixture.chunks.assign(num_segments, std::vector<uint8_t>(bytes_per_segment, kSliceFillByte));
    fixture.chunks[0][0] = 0x00U;
    fixture.chunks[0][1] = 0x00U;
    fixture.chunks[0][2] = 0x00U;
    fixture.chunks[0][3] = 0x01U;
    fixture.chunks[0][4] = 0x41U;
    fixture.nodes = LinkCodedSegments(fixture.chunks);
    fixture.total_input_bytes = num_segments * bytes_per_segment;
    return fixture;
}

SegmentChainFixture MakeIdrHeaderInjectFixture(size_t num_segments, size_t bytes_per_segment) {
    SegmentChainFixture fixture = MakeDeltaFixture(num_segments, bytes_per_segment);
    const std::vector<uint8_t> idr_prefix = {
        0x00U, 0x00U, 0x00U, 0x01U, 0x67U, 0x42U, 0xC0U, 0x1FU, 0x00U, 0x00U, 0x00U, 0x01U,
        0x68U, 0xCEU, 0x38U, 0x80U, 0x00U, 0x00U, 0x00U, 0x01U, 0x65U, 0x88U, 0x84U,
    };
    std::memcpy(fixture.chunks[0].data(), idr_prefix.data(), idr_prefix.size());
    return fixture;
}

template <typename AssembleFn>
void RunAssemblyBenchmark(benchmark::State& state, const SegmentChainFixture& fixture,
                          bool force_idr, AssembleFn assemble_fn) {
    for (auto _ : state) {
        auto encoded = assemble_fn(fixture.nodes.data(), force_idr, VAProfileH264High,
                                   fixture.sps_nalu, fixture.pps_nalu);
        benchmark::DoNotOptimize(encoded->data());
        benchmark::DoNotOptimize(encoded->size());
        benchmark::ClobberMemory();
    }

#if defined(VAAPI_TEST_ENABLE_HEAP_TRACKER)
    webrtc::scoped_refptr<webrtc::EncodedImageBuffer> held_output;
    HeapAllocStats stats{};
    {
        ScopedHeapTracker tracker;
        held_output = assemble_fn(fixture.nodes.data(), force_idr, VAProfileH264High,
                                  fixture.sps_nalu, fixture.pps_nalu);
        stats = tracker.Snapshot();
    }
    benchmark::DoNotOptimize(held_output->data());
    state.counters["transient_alloc_bytes"] = static_cast<double>(stats.transient_freed_bytes);
    state.counters["peak_live_bytes"] = static_cast<double>(stats.peak_live_usable_bytes);
#endif

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(fixture.total_input_bytes));
}

void RunScratchReuseBenchmark(benchmark::State& state, const SegmentChainFixture& fixture,
                              bool force_idr) {
    std::vector<uint8_t> scratch_bitstream;
    scratch_bitstream.reserve(fixture.total_input_bytes);
    RunAssemblyBenchmark(
            state, fixture, force_idr,
            [&scratch_bitstream](const void* coded_data, bool is_idr, VAProfile profile,
                                 absl::Span<const uint8_t> sps, absl::Span<const uint8_t> pps) {
                return VaapiVideoEncoderTestPeer::AssembleEncodedBitstream(
                        coded_data, is_idr, profile, sps, pps, scratch_bitstream);
            });
}

void BmAssembleDeltaSingleSegmentBaseline(benchmark::State& state) {
    const SegmentChainFixture fixture = MakeDeltaFixture(/*num_segments=*/1U, kSingleDeltaBytes);
    RunAssemblyBenchmark(state, fixture, /*force_idr=*/false, &AssembleEncodedBitstreamBaseline);
}

void BmAssembleDeltaSingleSegmentScratchReuse(benchmark::State& state) {
    const SegmentChainFixture fixture = MakeDeltaFixture(/*num_segments=*/1U, kSingleDeltaBytes);
    RunScratchReuseBenchmark(state, fixture, /*force_idr=*/false);
}

void BmAssembleDeltaMultiSegmentBaseline(benchmark::State& state) {
    const SegmentChainFixture fixture =
            MakeDeltaFixture(kMultiSegmentCount, kMultiDeltaSegmentBytes);
    RunAssemblyBenchmark(state, fixture, /*force_idr=*/false, &AssembleEncodedBitstreamBaseline);
}

void BmAssembleDeltaMultiSegmentScratchReuse(benchmark::State& state) {
    const SegmentChainFixture fixture =
            MakeDeltaFixture(kMultiSegmentCount, kMultiDeltaSegmentBytes);
    RunScratchReuseBenchmark(state, fixture, /*force_idr=*/false);
}

void BmAssembleIdrHeaderInjectBaseline(benchmark::State& state) {
    const SegmentChainFixture fixture =
            MakeIdrHeaderInjectFixture(kMultiSegmentCount, kMultiIdrSegmentBytes);
    RunAssemblyBenchmark(state, fixture, /*force_idr=*/true, &AssembleEncodedBitstreamBaseline);
}

void BmAssembleIdrHeaderInjectScratchReuse(benchmark::State& state) {
    const SegmentChainFixture fixture =
            MakeIdrHeaderInjectFixture(kMultiSegmentCount, kMultiIdrSegmentBytes);
    RunScratchReuseBenchmark(state, fixture, /*force_idr=*/true);
}

BENCHMARK(BmAssembleDeltaSingleSegmentBaseline);
BENCHMARK(BmAssembleDeltaSingleSegmentScratchReuse);
BENCHMARK(BmAssembleDeltaMultiSegmentBaseline);
BENCHMARK(BmAssembleDeltaMultiSegmentScratchReuse);
BENCHMARK(BmAssembleIdrHeaderInjectBaseline);
BENCHMARK(BmAssembleIdrHeaderInjectScratchReuse);

}  // namespace
}  // namespace goldfish::videobridge

BENCHMARK_MAIN();
