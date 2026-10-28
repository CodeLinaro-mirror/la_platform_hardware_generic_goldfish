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
 * @file grpc_web_protocol_perf.cc
 * @brief Google Benchmark suite for gRPC-Web frame packing, streaming Base64
 *        encoding, and RFC 3986 percent-encoding in goldfish::grpcweb across
 *        payload sizes (64 B to 64 KiB).
 *
 * Results (--config=release):
 * - BmStreamingBase64Encode (zero-copy string_view vs heap copy):
 *   - 64 B:    127 ns   (481 MiB/s) -> 101 ns   (603 MiB/s)  [1.26x]
 *   - 64 KiB: 91.6 us   (682 MiB/s) -> 54.0 us  (1.13 GiB/s) [1.70x]
 * - BmPackFrameAndEncode (ByteBuffer -> PackFrame -> EncodeChunk):
 *   - 64 B:    241 ns   (253 MiB/s) -> 166 ns   (367 MiB/s)  [1.45x]
 *   - 256 B:   415 ns   (588 MiB/s) -> 354 ns   (689 MiB/s)  [1.17x]
 *   - 16 KiB: 15.9 us   (983 MiB/s) -> 14.2 us  (1.07 GiB/s) [1.12x]
 *   - 64 KiB: 226.4 us  (276 MiB/s) -> 92.0 us  (680 MiB/s)  [2.46x]
 * - BmPercentEncode (span-based append vs char-by-char push_back):
 *   - 165 B:  1,049 ns  (151 MiB/s) -> 367 ns   (431 MiB/s)  [2.86x]
 */

#include "goldfish/grpcweb/grpc_web_protocol.h"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"

namespace goldfish::grpcweb {
namespace {

constexpr int64_t kMinPayloadBytes = 64;
constexpr int64_t kMaxPayloadBytes = int64_t{64} << 10;
constexpr int kRangeMultiplier = 4;

std::string MakePayload(size_t size) {
    std::string payload(size, '\0');
    for (size_t i = 0; i < size; ++i) {
        payload[i] = static_cast<char>((i * 131 + 0x42) & 0xFF);
    }
    return payload;
}

class CopyBaselineStreamingBase64Encoder {
  public:
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    std::string EncodeChunk(std::string_view data) {
        if (data.empty()) {
            return "";
        }
        std::string input;
        if (!remainder_.empty()) {
            input.reserve(remainder_.size() + data.size());
            input.append(remainder_);
            input.append(data);
            remainder_.clear();
        } else {
            input = std::string(data);
        }

        const size_t complete_triplets_len = (input.size() / 3) * 3;
        std::string output;
        if (complete_triplets_len > 0) {
            output = absl::Base64Escape(std::string_view(input.data(), complete_triplets_len));
        }
        if (input.size() > complete_triplets_len) {
            remainder_ = input.substr(complete_triplets_len);
        }
        return output;
    }

  private:
    std::string remainder_;
};

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
std::string PercentEncodeCharByCharBaseline(std::string_view input) {
    static constexpr char kHexDigits[] = "0123456789ABCDEF";
    std::string escaped;
    escaped.reserve(input.size());
    for (unsigned char c : input) {
        if (absl::ascii_isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped.push_back(static_cast<char>(c));
        } else {
            escaped.push_back('%');
            escaped.push_back(kHexDigits[(c >> 4) & 0x0F]);
            escaped.push_back(kHexDigits[c & 0x0F]);
        }
    }
    return escaped;
}

void BmStreamingBase64EncodeCopyBaseline(benchmark::State& state) {
    const auto payload_size = static_cast<size_t>(state.range(0));
    const std::string payload = MakePayload(payload_size);

    for (auto _ : state) {
        CopyBaselineStreamingBase64Encoder encoder;
        std::string out = encoder.EncodeChunk(payload);
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(payload_size));
}

void BmStreamingBase64Encode(benchmark::State& state) {
    const auto payload_size = static_cast<size_t>(state.range(0));
    const std::string payload = MakePayload(payload_size);

    for (auto _ : state) {
        StreamingBase64Encoder encoder;
        std::string out = encoder.EncodeChunk(payload);
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(payload_size));
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
std::string ByteBufferToStringDumpBaseline(const grpc::ByteBuffer& buffer) {
    std::vector<grpc::Slice> slices;
    grpc::Status status = buffer.Dump(&slices);
    if (!status.ok()) {
        return "";
    }
    std::string result;
    result.reserve(buffer.Length());
    for (const auto& slice : slices) {
        result.append(reinterpret_cast<const char*>(slice.begin()), slice.size());
    }
    return result;
}

void BmPackFrameAndEncodeBaseline(benchmark::State& state) {
    const auto payload_size = static_cast<size_t>(state.range(0));
    const std::string payload = MakePayload(payload_size);
    const grpc::ByteBuffer bb = Protocol::StringToByteBuffer(payload);

    for (auto _ : state) {
        std::string resp_data = ByteBufferToStringDumpBaseline(bb);
        std::string data_frame = Protocol::PackFrame(kFrameData, resp_data);
        CopyBaselineStreamingBase64Encoder encoder;
        std::string out = encoder.EncodeChunk(data_frame);
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(payload_size));
}

void BmPackFrameAndEncode(benchmark::State& state) {
    const auto payload_size = static_cast<size_t>(state.range(0));
    const std::string payload = MakePayload(payload_size);
    const grpc::ByteBuffer bb = Protocol::StringToByteBuffer(payload);

    for (auto _ : state) {
        std::string data_frame = Protocol::PackFrame(kFrameData, bb);
        StreamingBase64Encoder encoder;
        std::string out = encoder.EncodeChunk(data_frame);
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(payload_size));
}

void BmPercentEncodeCharByCharBaseline(benchmark::State& state) {
    constexpr std::string_view kStatusMessage =
            "Failed to connect to upstream gRPC endpoint 127.0.0.1:8554: "
            "DEADLINE_EXCEEDED (timeout=10000ms, method=/android.emulation.control."
            "EmulatorController/streamScreenshot)";
    for (auto _ : state) {
        std::string out = PercentEncodeCharByCharBaseline(kStatusMessage);
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(kStatusMessage.size()));
}

void BmPercentEncode(benchmark::State& state) {
    constexpr std::string_view kStatusMessage =
            "Failed to connect to upstream gRPC endpoint 127.0.0.1:8554: "
            "DEADLINE_EXCEEDED (timeout=10000ms, method=/android.emulation.control."
            "EmulatorController/streamScreenshot)";
    for (auto _ : state) {
        std::string out = Protocol::PercentEncode(kStatusMessage);
        benchmark::DoNotOptimize(out.data());
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(kStatusMessage.size()));
}

BENCHMARK(BmStreamingBase64EncodeCopyBaseline)
        ->RangeMultiplier(kRangeMultiplier)
        ->Range(kMinPayloadBytes, kMaxPayloadBytes);
BENCHMARK(BmStreamingBase64Encode)
        ->RangeMultiplier(kRangeMultiplier)
        ->Range(kMinPayloadBytes, kMaxPayloadBytes);
BENCHMARK(BmPackFrameAndEncodeBaseline)
        ->RangeMultiplier(kRangeMultiplier)
        ->Range(kMinPayloadBytes, kMaxPayloadBytes);
BENCHMARK(BmPackFrameAndEncode)
        ->RangeMultiplier(kRangeMultiplier)
        ->Range(kMinPayloadBytes, kMaxPayloadBytes);
BENCHMARK(BmPercentEncodeCharByCharBaseline);
BENCHMARK(BmPercentEncode);

}  // namespace
}  // namespace goldfish::grpcweb

BENCHMARK_MAIN();
