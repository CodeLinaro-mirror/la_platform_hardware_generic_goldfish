// Copyright 2026 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0

/**
 * @file websocket_perf.cc
 * @brief Google Benchmark suite for RFC 6455 WebSocket frame masking and
 *        encoding in goldfish::netsim::websocket across packet payload sizes
 *        (64 B to 64 KiB).
 *
 * Results (--config=release):
 * - ApplyMask (in-place payload masking):
 *   - 64 B:    60.4 ns (1.01 GiB/s) ->   9.81 ns ( 6.08 GiB/s)  [ 6.16x]
 *   - 256 B:   226 ns  (1.06 GiB/s) ->  16.9 ns  (14.13 GiB/s)  [13.37x]
 *   - 1 KiB:   938 ns  (1.02 GiB/s) ->  48.5 ns  (19.67 GiB/s)  [19.34x]
 *   - 4 KiB:  3.63 us  (1.05 GiB/s) -> 159 ns    (23.98 GiB/s)  [22.81x]
 *   - 16 KiB: 13.9 us  (1.10 GiB/s) -> 689 ns    (22.13 GiB/s)  [20.16x]
 *   - 64 KiB: 56.5 us  (1.08 GiB/s) -> 3.08 us   (19.80 GiB/s)  [18.31x]
 * - EncodeFrame (header + payload allocation + copy + masking):
 *   - 64 B:    114 ns  (537 MiB/s)  ->  60.5 ns  (1,009 MiB/s)  [ 1.88x]
 *   - 256 B:   294 ns  (830 MiB/s)  ->  60.0 ns  ( 3.97 GiB/s)  [ 4.90x]
 *   - 1 KiB:   961 ns  (1.02 GiB/s) -> 109 ns    ( 8.73 GiB/s)  [ 8.82x]
 *   - 4 KiB:  8.16 us  (479 MiB/s)  -> 331 ns    (11.52 GiB/s)  [24.66x]
 *   - 16 KiB: 14.4 us  (1.06 GiB/s) -> 1.14 us   (13.38 GiB/s)  [12.64x]
 *   - 64 KiB: 60.6 us  (1.01 GiB/s) -> 7.87 us   ( 7.76 GiB/s)  [ 7.70x]
 */

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "emulator/plugin/netsim/websocket_transport/websocket.h"

namespace goldfish::netsim::websocket {
namespace {

constexpr int64_t kMinPayloadBytes = 64;
constexpr int64_t kMaxPayloadBytes = int64_t{64} << 10;
constexpr int kRangeMultiplier = 4;
constexpr uint8_t kPayloadSeed = 0x42;
constexpr MaskKey kBenchmarkMask{{0x12, 0x34, 0x56, 0x78}};

std::vector<uint8_t> MakePayload(size_t size) {
    std::vector<uint8_t> payload(size);
    for (size_t i = 0; i < size; ++i) {
        payload[i] = static_cast<uint8_t>(kPayloadSeed + i);
    }
    return payload;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void ApplyMaskByteByByteBaseline(std::span<uint8_t> data, MaskKey mask) {
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] ^= mask.bytes[i % 4];
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
std::vector<uint8_t> EncodeFrameUnreservedBaseline(Opcode opcode, std::span<const uint8_t> payload,
                                                   std::optional<MaskKey> mask, bool fin = true) {
    FrameHeader header = EncodeHeader(opcode, payload.size(), mask, fin);
    std::vector<uint8_t> out(header.span().begin(), header.span().end());
    out.insert(out.end(), payload.begin(), payload.end());
    if (mask) {
        ApplyMaskByteByByteBaseline(std::span(out).subspan(header.size), *mask);
    }
    return out;
}

void BM_ApplyMask_ByteByByteBaseline(benchmark::State& state) {
    const auto payload_size = static_cast<size_t>(state.range(0));
    std::vector<uint8_t> data = MakePayload(payload_size);

    for (auto _ : state) {
        ApplyMaskByteByByteBaseline(data, kBenchmarkMask);
        benchmark::DoNotOptimize(data.data());
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(payload_size));
}

void BM_ApplyMask(benchmark::State& state) {
    const auto payload_size = static_cast<size_t>(state.range(0));
    std::vector<uint8_t> data = MakePayload(payload_size);

    for (auto _ : state) {
        ApplyMask(data, kBenchmarkMask);
        benchmark::DoNotOptimize(data.data());
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(payload_size));
}

void BM_EncodeFrame_UnreservedBaseline(benchmark::State& state) {
    const auto payload_size = static_cast<size_t>(state.range(0));
    const std::vector<uint8_t> payload = MakePayload(payload_size);

    for (auto _ : state) {
        std::vector<uint8_t> frame =
                EncodeFrameUnreservedBaseline(Opcode::kBinary, payload, kBenchmarkMask);
        benchmark::DoNotOptimize(frame.data());
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(payload_size));
}

void BM_EncodeFrame(benchmark::State& state) {
    const auto payload_size = static_cast<size_t>(state.range(0));
    const std::vector<uint8_t> payload = MakePayload(payload_size);

    for (auto _ : state) {
        std::vector<uint8_t> frame = EncodeFrame(Opcode::kBinary, payload, kBenchmarkMask);
        benchmark::DoNotOptimize(frame.data());
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(payload_size));
}

BENCHMARK(BM_ApplyMask_ByteByByteBaseline)
        ->RangeMultiplier(kRangeMultiplier)
        ->Range(kMinPayloadBytes, kMaxPayloadBytes);
BENCHMARK(BM_ApplyMask)
        ->RangeMultiplier(kRangeMultiplier)
        ->Range(kMinPayloadBytes, kMaxPayloadBytes);
BENCHMARK(BM_EncodeFrame_UnreservedBaseline)
        ->RangeMultiplier(kRangeMultiplier)
        ->Range(kMinPayloadBytes, kMaxPayloadBytes);
BENCHMARK(BM_EncodeFrame)
        ->RangeMultiplier(kRangeMultiplier)
        ->Range(kMinPayloadBytes, kMaxPayloadBytes);

}  // namespace
}  // namespace goldfish::netsim::websocket

BENCHMARK_MAIN();
