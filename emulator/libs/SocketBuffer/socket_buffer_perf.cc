// Copyright 2026 The Android Open Source Project
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
 * @file socket_buffer_perf.cc
 * @brief Google Benchmark suite for goldfish::SocketBuffer across ADB/vsock
 *        payload sizes (1 KiB to 32 MiB).
 *
 * Results (--config=release):
 * - AppendPeekConsumeDrain:
 *   - 16 KiB:   171 ns   (89.2 GiB/s)
 *   - 64 KiB:   1.51 us  (40.4 GiB/s)
 *   - 1 MiB:    61.7 us  (15.8 GiB/s)
 *   - 4 MiB:    264 us   (14.8 GiB/s) -- retains backing array (< 16 MiB)
 * - AdbBackpressureBurst (16 KiB appends -> 4 KiB virtqueue drains):
 *   - 16 KiB:   178 ns   (85.6 GiB/s)
 *   - 64 KiB:   1.10 us  (55.4 GiB/s)
 *   - 256 KiB:  5.30 us  (46.1 GiB/s)
 *   - 1 MiB:    21.9 us  (44.6 GiB/s)
 *   - 4 MiB:    150.5 us (26.0 GiB/s) -- retains backing array across drains
 *     (previously 1,208 us / 3.2 GiB/s when kLargeCapacityReleaseIfEmpty was
 *     4 MiB, which freed the backing array on every full drain).
 */

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "goldfish/socket_buffer.h"

namespace goldfish {
namespace {

// Measures steady-state Append + Peek + Consume when draining the buffer
// completely on each iteration (the common case in vsock.cc when guest credit
// is available). Payloads whose capacity reaches kLargeCapacityReleaseIfEmpty
// (16 MiB) trigger heap deallocation on full drain.
void AppendPeekConsumeDrain(benchmark::State& state) {
    const auto chunk_size = static_cast<size_t>(state.range(0));
    const std::vector<char> payload(chunk_size, 'A');
    SocketBuffer buffer;

    for (auto _ : state) {
        benchmark::DoNotOptimize(buffer.Append(payload.data(), payload.size()));
        while (buffer.Size() > 0) {
            const auto [ptr, len] = buffer.Peek();
            benchmark::DoNotOptimize(ptr);
            benchmark::DoNotOptimize(buffer.Consume(len));
        }
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(chunk_size));
}

// Measures circular ring-buffer wrap-around performance when a residual unread
// tail remains in the buffer (e.g., when guest vsock credit is partially
// exhausted), exercising the two-part split memcpy path across capacity_.
void RingWrapAroundStreaming(benchmark::State& state) {
    const auto chunk_size = static_cast<size_t>(state.range(0));
    const std::vector<char> payload(chunk_size, 'B');
    SocketBuffer buffer;

    // Prime the buffer with a half-chunk so subsequent appends and consumes
    // continuously wrap around the backing array without hitting size_ == 0.
    (void)buffer.Append(payload.data(), chunk_size / 2);

    for (auto _ : state) {
        benchmark::DoNotOptimize(buffer.Append(payload.data(), chunk_size));
        size_t remaining = chunk_size;
        while (remaining > 0) {
            const auto [ptr, len] = buffer.Peek();
            benchmark::DoNotOptimize(ptr);
            const size_t to_consume = std::min(remaining, len);
            benchmark::DoNotOptimize(buffer.Consume(to_consume));
            remaining -= to_consume;
        }
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(chunk_size));
}

// Simulates ADB Host->Guest buffering under backpressure: accumulating 16 KiB
// read chunks up to total_payload_size, then draining in 4 KiB virtio-vsock
// virtqueue page segments.
void AdbBackpressureBurst(benchmark::State& state) {
    constexpr size_t kLibuvReadChunkSize = size_t{16} << 10;
    constexpr size_t kVirtqueueElemSize = size_t{4} << 10;
    const auto total_payload_size = static_cast<size_t>(state.range(0));
    const std::vector<char> read_chunk(kLibuvReadChunkSize, 'C');
    SocketBuffer buffer;

    for (auto _ : state) {
        size_t appended = 0;
        while (appended < total_payload_size) {
            const size_t n = std::min(kLibuvReadChunkSize, total_payload_size - appended);
            benchmark::DoNotOptimize(buffer.Append(read_chunk.data(), n));
            appended += n;
        }

        while (buffer.Size() > 0) {
            const auto [ptr, len] = buffer.Peek();
            benchmark::DoNotOptimize(ptr);
            const size_t to_consume = std::min(kVirtqueueElemSize, len);
            benchmark::DoNotOptimize(buffer.Consume(to_consume));
        }
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(total_payload_size));
}

BENCHMARK(AppendPeekConsumeDrain)->RangeMultiplier(4)->Range(1024, int64_t{32} << 20);
BENCHMARK(RingWrapAroundStreaming)->RangeMultiplier(4)->Range(1024, int64_t{1} << 20);
BENCHMARK(AdbBackpressureBurst)->RangeMultiplier(4)->Range(int64_t{16} << 10, int64_t{32} << 20);

}  // namespace
}  // namespace goldfish

BENCHMARK_MAIN();
