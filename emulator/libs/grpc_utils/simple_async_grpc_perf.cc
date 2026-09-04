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
 * @file simple_async_grpc_perf.cc
 * @brief Microbenchmarks comparing WithSimpleQueueWriter streaming throughput
 * and latency with and without message buffer recycling.
 *
 * ## Machine Specifications
 *
 * - **Processor:** Apple M4 Max (Mac16,5)
 * - **Cores:** 16 Cores (12 Performance + 4 Efficiency) @ 24 MHz timebase
 * - **Memory:** 64 GB Unified RAM
 * - **Caches:** L1 Data 64 KiB, L1 Instruction 128 KiB, L2 Unified 4096 KiB
 *
 * ## Benchmark Results (`bazel run -c opt`)
 *
 * Buffer recycling eliminates heap allocations during streaming:
 *
 * | Payload | No Recycle | With Recycle | Speedup  | Bandwidth (Recycle) |
 * |:--------|:-----------|:-------------|:---------|:--------------------|
 * | 64 KB   | 2,298 ns   | 328 ns       | **7.0x** | 185.3 GiB/s         |
 * | 1 MB    | 66.3 us    | 4.7 us       | **14.2x**| 203.6 GiB/s         |
 * | 8.3 MB  | 705.6 us   | 37.1 us      | **19.0x**| 208.6 GiB/s         |
 */
#include <benchmark/benchmark.h>

#include <string>

#include "android/emulation/control/simple_async_grpc.h"

namespace android::emulation::control {

struct MockPayload {
    uint64_t sequence_number{0};
    std::string data;
};

template <typename W>
class FakeBenchmarkWriteReactor {
  public:
    virtual ~FakeBenchmarkWriteReactor() = default;
    virtual void OnWriteDone(bool ok) = 0;
    virtual void OnDone() {}

    void StartWrite(const W* msg) { benchmark::DoNotOptimize(msg); }
};

// Benchmark streaming writes WITHOUT recycling (recycle_size = 0)
static void BmStreamingWithoutRecycling(benchmark::State& state) {
    const size_t payload_size = state.range(0);
    WithSimpleQueueWriter<FakeBenchmarkWriteReactor<MockPayload>, /*max_queue_size=*/2,
                          /*recycle_size=*/0>
            writer;

    for (auto _ : state) {
        MockPayload msg;
        msg.sequence_number++;
        msg.data.resize(payload_size, 'x');
        writer.Write(std::move(msg));

        // Simulate gRPC write completion
        writer.OnWriteDone(true);
    }
    state.SetBytesProcessed(state.iterations() * payload_size);
    state.SetItemsProcessed(state.iterations());
}

// Benchmark streaming writes WITH recycling (recycle_size = 2)
static void BmStreamingWithRecycling(benchmark::State& state) {
    const size_t payload_size = state.range(0);
    WithSimpleQueueWriter<FakeBenchmarkWriteReactor<MockPayload>, /*max_queue_size=*/2,
                          /*recycle_size=*/2>
            writer;

    for (auto _ : state) {
        MockPayload msg = writer.AcquireMessage();
        msg.sequence_number++;
        msg.data.resize(payload_size, 'x');
        writer.Write(std::move(msg));

        // Simulate gRPC write completion (recycles the message)
        writer.OnWriteDone(true);
    }
    state.SetBytesProcessed(state.iterations() * payload_size);
    state.SetItemsProcessed(state.iterations());
}

// 64 KB (Audio / Thumbnail), 1 MB (PNG / low-res frame), 8.3 MB (1080p uncompressed RGBA)
BENCHMARK(BmStreamingWithoutRecycling)
        ->Arg(64LL * 1024)
        ->Arg(1024LL * 1024)
        ->Arg(1920LL * 1080 * 4);
BENCHMARK(BmStreamingWithRecycling)->Arg(64LL * 1024)->Arg(1024LL * 1024)->Arg(1920LL * 1080 * 4);

}  // namespace android::emulation::control
