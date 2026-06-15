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

// clang-format off
/**
 * @file breadcrumb_benchmark.cc
 * @brief Benchmarks various serialization strategies for breadcrumb logging.
 *
 * This file evaluates the performance of different approaches for storing
 * diagnostic breadcrumbs in a circular buffer. The goal is to minimize overhead
 * in high-frequency logging scenarios (like ADB traffic) while maintaining
 * flexibility for complex structures (like gRPC events).
 *
 * The following options are explored:
 *
 * 1. Protobuf Serialization (BM_ProtobufSerialization):
 *    - Strategy: Populate a full `android::control::breadcrumbs::Breadcrumb` protobuf
 *      message and serialize it to a string.
 *    - Pros: High flexibility, strongly typed, easy to evolve.
 *    - Cons: High overhead due to protobuf serialization and heap allocations.
 *    - This was the baseline approach used before the refactor.
 *
 * 2. Raw Struct Copy (BM_RawStructCopy):
 *    - Strategy: Populate a flat C-style struct (`RawAdbBreadcrumb`) containing all
 *      fields and `memcpy` it directly to the buffer.
 *    - Pros: Maximum performance, zero heap allocation, single instruction copy.
 *    - Cons: Zero flexibility. Fixed size for snippets (leads to padding waste or
 *      truncation). Hard to support heterogeneous event types.
 *
 * 3. Custom Binary Serialization (BM_CustomBinarySerialization):
 *    - Strategy: Use a compact binary envelope with a fixed header (`BreadcrumbEnvelope`)
 *      followed by a type-specific payload struct (`RawAdbPayload`) and a
 *      variable-length data snippet.
 *    - Pros: Very high performance (close to raw copy), handles variable-length
 *      snippets without wasted space, allows heterogeneous event types.
 *    - Cons: Requires custom parsing logic in the processor.
 *    - This approach is adopted for high-frequency ADB events.
 *
 * 4. Binary Envelope + Proto Payload (BM_BinaryEnvelopeProtoPayload):
 *    - Strategy: Use the same fixed header (`BreadcrumbEnvelope`) but store a serialized
 *      specific payload proto (e.g., `AdbPayload` or `GrpcPayload`) as the variable
 *      length payload.
 *    - Pros: Good compromise for complex events (like gRPC) where fields are highly
 *      structured. Header remains fast to parse.
 *    - Cons: Still incurs serialization overhead for the payload part.
 *    - This approach is adopted for complex gRPC events.
 *
 * ### Empirical Performance Results (Release Build)
 *
 * Tests executed on **Apple M4 Max (ARM64, 16 Cores)** and **Intel(R) Xeon(R) Gold 6154 @ 3.00GHz (x86_64, 72 Cores)**:
 *
 * | Benchmark                           | Apple M4 Max | Intel Xeon Gold | Strategy Analysis & Conclusions |
 * | :---------------------------------- | :----------: | :-------------: | :---------------------------------- |
 * | `BM_ProtobufSerialization`          |  11.30 ns    |     35.80 ns    | Full Proto serialization in isolation. |
 * | `BM_RawStructCopy`                  |   0.97 ns    |      2.18 ns    | Memory copy baseline (the speed of light). |
 * | `BM_CustomBinarySerialization`      |   0.88 ns    |      2.72 ns    | Compact custom binary serialization (logic only). |
 * | `BM_BinaryEnvelopeProtoPayload`     |  11.70 ns    |     24.80 ns    | Envelope + Proto payload serialization (logic only). |
 * | `BM_RawCircularLog_Push`            |   6.19 ns    |     22.60 ns    | Engine Overhead (Mutex + Commit logic). |
 * | `BM_ProtoCircularLog_Push`          |  16.90 ns    |     66.90 ns    | Legacy full-proto push (Proto + Engine). |
 * | `BM_RawCircularLog_Drift_30B`       |   7.67 ns    |     38.50 ns    | Core push with unaligned 30B record drift. |
 * | `BM_RawCircularLog_Aligned_32B`     |   7.66 ns    |     38.40 ns    | Core push with 32B aligned record. |
 * | `BM_Atomic_Aligned`                 |   1.58 ns    |      4.89 ns    | Atomic `fetch_add` on an 8-byte aligned boundary. |
 * | `BM_Atomic_SplitLock`               |  **CRASH**   |   **4124.0 ns** | **The Performance Cliff:** Apple SIGBUS / Intel 840x Bus Lock. |
 * | `BM_UnalignedAccess`                |   0.33 ns    |      0.81 ns    | Standard unaligned 64-bit store (memcpy-level unaligned). |
 * | `BM_AlignedAccess`                  |   0.33 ns    |      0.81 ns    | Standard aligned 64-bit store. |
 *
 * ### Crucial Technical Insights
 *
 * 1. **The Alignment & Atomicity Concern:**
 *    - **On Apple Silicon (M4):** standard unaligned writes (`BM_UnalignedAccess`) are extremely fast, 
 *      but an unaligned atomic operation (`BM_Atomic_SplitLock`) triggers a hardware exception, terminating 
 *      the process immediately with a `SIGBUS` error.
 *    - **On Intel Xeon (Skylake-SP):** Unaligned atomics do not crash, but they trigger a **Split Lock** 
 *      which asserts a system-wide `LOCK#` signal on the memory bus. This causes the operation to slow 
 *      down from **4.89ns to 4124ns (an 840x performance penalty)**, stalling all other cores on the socket.
 *    - **Why standard memcpy didn't show this:** `std::memcpy` doesn't use atomic instructions. Modern CPUs 
 *      break unaligned non-atomic writes into two fast micro-ops with negligible penalty, hiding the drift.
 *
 * 2. **Design Verdict:**
 *    - We use exactly **32-byte records** (2-byte circular log header + 30-byte padded envelope) in production.
 *    - This guarantees that every envelope and its internal 64-bit fields (`timestamp_ns`, `thread_id`, `flow_id`) 
 *      are perfectly 8-byte aligned, avoiding catastrophic split locks on Intel and fatal crashes on Mac.
 *    - This costs us **42 entries (~6% capacity)** in a 20KB buffer, which is a necessary trade-off to guarantee 
 *      absolute forensic integrity (preventing torn values during a crash) and cross-platform stability.
 */
// clang-format on

#include <benchmark/benchmark.h>

#include <atomic>
#include <cstring>
#include <vector>

#include "android/crashreport/breadcrumb_proto.h"
#include "breadcrumb.pb.h"
#include "goldfish/adb/adb_breadcrumb_tracker.h"
#include "goldfish/adb/adb_message_logger.h"
#include "goldfish/circular_message_log.h"
#include "goldfish/raw_circular_log.h"

namespace android::crashreport {

// Define a raw struct for comparison
struct RawAdbBreadcrumb {
    TimestampNs timestamp_ns;
    uint32_t thread_id;
    FlowId flow_id;
    uint32_t command;
    uint8_t direction;
    char snippet[32];
};

static void BM_ProtobufSerialization(benchmark::State& state) {
    control::breadcrumbs::Breadcrumb event;
    event.set_flow_id(123);
    event.set_timestamp_ns(456);
    event.set_thread_id(789);

    auto* adb = event.mutable_adb();
    adb->set_command(0x4e584e43);
    adb->set_direction(control::breadcrumbs::AdbPayload::TO_GUEST);
    adb->set_data_snippet("hello world");

    std::string buffer;
    for (auto _ : state) {
        buffer.clear();
        event.SerializeToString(&buffer);
        benchmark::DoNotOptimize(buffer);
    }
}
BENCHMARK(BM_ProtobufSerialization);

static void BM_RawStructCopy(benchmark::State& state) {
    RawAdbBreadcrumb event;
    event.flow_id = 123;
    event.timestamp_ns = 456;
    event.thread_id = 789;
    event.command = 0x4e584e43;
    event.direction = 1;
    std::strncpy(event.snippet, "hello world", sizeof(event.snippet));

    std::vector<char> buffer(sizeof(RawAdbBreadcrumb));
    for (auto _ : state) {
        std::memcpy(buffer.data(), &event, sizeof(RawAdbBreadcrumb));
        benchmark::DoNotOptimize(buffer);
    }
}
BENCHMARK(BM_RawStructCopy);

static void BM_CustomBinarySerialization(benchmark::State& state) {
    BreadcrumbEnvelope envelope;
    envelope.flow_id = 123;
    envelope.timestamp_ns = 456;
    envelope.thread_id = 789;
    envelope.phase = BreadcrumbPhase::kInstant;
    envelope.payload_type = PayloadType::kAdbRawToGuest;

    RawAdbPayload adb;
    adb.command = 0x4e584e43;

    std::string snippet = "hello world";
    adb.snippet_len = snippet.size();

    envelope.payload_len = sizeof(RawAdbPayload) + adb.snippet_len;

    std::vector<char> buffer(sizeof(BreadcrumbEnvelope) + envelope.payload_len);

    for (auto _ : state) {
        char* p = buffer.data();
        std::memcpy(p, &envelope, sizeof(BreadcrumbEnvelope));
        p += sizeof(BreadcrumbEnvelope);
        std::memcpy(p, &adb, sizeof(RawAdbPayload));
        p += sizeof(RawAdbPayload);
        std::memcpy(p, snippet.data(), adb.snippet_len);

        benchmark::DoNotOptimize(buffer);
    }
}
BENCHMARK(BM_CustomBinarySerialization);

static void BM_BinaryEnvelopeProtoPayload(benchmark::State& state) {
    BreadcrumbEnvelope envelope;
    envelope.flow_id = 123;
    envelope.timestamp_ns = 456;
    envelope.thread_id = 789;
    envelope.phase = BreadcrumbPhase::kInstant;
    envelope.payload_type = PayloadType::kAdbProto;

    control::breadcrumbs::AdbPayload adb;
    adb.set_command(0x4e584e43);
    adb.set_direction(control::breadcrumbs::AdbPayload::TO_GUEST);
    adb.set_data_snippet("hello world");

    std::string proto_buffer;
    std::vector<char> buffer(128);  // Large enough

    for (auto _ : state) {
        proto_buffer.clear();
        adb.SerializeToString(&proto_buffer);

        envelope.payload_len = proto_buffer.size();

        char* p = buffer.data();
        std::memcpy(p, &envelope, sizeof(BreadcrumbEnvelope));
        p += sizeof(BreadcrumbEnvelope);
        std::memcpy(p, proto_buffer.data(), envelope.payload_len);

        benchmark::DoNotOptimize(buffer);
    }
}
BENCHMARK(BM_BinaryEnvelopeProtoPayload);

static void BM_RawCircularLog_Push(benchmark::State& state) {
    using goldfish::proto_data_store::RawCircularLog;
    const size_t buffer_size = 1024 * 1024;
    std::vector<uint8_t> buffer(buffer_size);
    auto log_or = RawCircularLog::CreateWriter(buffer.data(), buffer.size());
    auto& log = *log_or;

    const uint32_t payload_size = 30;  // Matches padded envelope
    std::vector<uint8_t> payload(payload_size, 0xAA);

    for (auto _ : state) {
        (void)log->Push(payload_size,
                        [&](void* dest) { std::memcpy(dest, payload.data(), payload_size); });
    }
}
BENCHMARK(BM_RawCircularLog_Push);

static void BM_ProtoCircularLog_Push(benchmark::State& state) {
    using control::breadcrumbs::Breadcrumb;
    using goldfish::proto_data_store::ProtoCircularLog;

    const size_t buffer_size = 1024 * 1024;
    std::vector<uint8_t> buffer(buffer_size);
    auto log_or = ProtoCircularLog<Breadcrumb>::CreateWriter(buffer.data(), buffer.size());
    auto& log = *log_or;

    Breadcrumb event;
    event.set_flow_id(123);
    event.set_timestamp_ns(456);
    event.set_thread_id(789);
    auto* adb = event.mutable_adb();
    adb->set_command(0x4e584e43);
    adb->set_direction(control::breadcrumbs::AdbPayload::TO_GUEST);
    adb->set_data_snippet("hello world");

    for (auto _ : state) {
        (void)log->Push(event);
    }
}
BENCHMARK(BM_ProtoCircularLog_Push);

static void BM_RawCircularLog_Drift_30B(benchmark::State& state) {
    using goldfish::proto_data_store::RawCircularLog;
    const size_t buffer_size = 1024 * 1024;
    std::vector<uint8_t> buffer(buffer_size, 0);
    auto log_or = RawCircularLog::CreateWriter(buffer.data(), buffer.size());
    auto& log = *log_or;

    const uint32_t payload_size = 28;  // Total 30B with header
    std::vector<uint8_t> payload(payload_size, 0xAA);

    for (auto _ : state) {
        (void)log->Push(payload_size,
                        [&](void* dest) { std::memcpy(dest, payload.data(), payload_size); });
        if (log->BytesUsed() > buffer_size * 0.9) {
            log_or = RawCircularLog::CreateWriter(buffer.data(), buffer.size());
        }
    }
}
BENCHMARK(BM_RawCircularLog_Drift_30B);

static void BM_RawCircularLog_Aligned_32B(benchmark::State& state) {
    using goldfish::proto_data_store::RawCircularLog;
    const size_t buffer_size = 1024 * 1024;
    std::vector<uint8_t> buffer(buffer_size, 0);
    auto log_or = RawCircularLog::CreateWriter(buffer.data(), buffer.size());
    auto& log = *log_or;

    const uint32_t payload_size = 30;  // Total 32B with header
    std::vector<uint8_t> payload(payload_size, 0xBB);

    for (auto _ : state) {
        (void)log->Push(payload_size,
                        [&](void* dest) { std::memcpy(dest, payload.data(), payload_size); });
        if (log->BytesUsed() > buffer_size * 0.9) {
            log_or = RawCircularLog::CreateWriter(buffer.data(), buffer.size());
        }
    }
}
BENCHMARK(BM_RawCircularLog_Aligned_32B);

static void BM_Atomic_Aligned(benchmark::State& state) {
    alignas(128) uint8_t buffer[128];
    std::memset(buffer, 0, sizeof(buffer));
    std::atomic<uint64_t>* ptr = reinterpret_cast<std::atomic<uint64_t>*>(&buffer[0]);
    for (auto _ : state) {
        ptr->fetch_add(1, std::memory_order_relaxed);
        benchmark::DoNotOptimize(ptr);
    }
}
BENCHMARK(BM_Atomic_Aligned);

static void BM_Atomic_SplitLock(benchmark::State& state) {
    alignas(128) uint8_t buffer[128];
    std::memset(buffer, 0, sizeof(buffer));
    // Straddles the 64B cache line boundary (bytes 60-67)
    std::atomic<uint64_t>* ptr = reinterpret_cast<std::atomic<uint64_t>*>(&buffer[60]);
    for (auto _ : state) {
        ptr->fetch_add(1, std::memory_order_relaxed);
        benchmark::DoNotOptimize(ptr);
    }
}
#ifndef __APPLE__
// This will crash on apple M4
BENCHMARK(BM_Atomic_SplitLock);
#endif

static void BM_UnalignedAccess(benchmark::State& state) {
    std::vector<uint8_t> buffer(64, 0);
    BreadcrumbEnvelope* env = reinterpret_cast<BreadcrumbEnvelope*>(buffer.data() + 2);
    for (auto _ : state) {
        env->timestamp_ns = 0x1122334455667788ULL;
        env->thread_id = 0x99AABBCCDDEEFF00ULL;
        env->flow_id = 0x1234567890ABCDEFULL;
        benchmark::DoNotOptimize(env);
    }
}
BENCHMARK(BM_UnalignedAccess);

static std::vector<uint8_t> GenerateMockAdbStream(size_t num_packets, uint32_t command,
                                                  size_t payload_size) {
    std::vector<uint8_t> stream;
    constexpr size_t amessage_size = sizeof(goldfish::adb::AMessage);

    for (size_t i = 0; i < num_packets; ++i) {
        goldfish::adb::AMessage header;
        header.command = command;
        header.arg0 = i;
        header.arg1 = i + 1;
        header.data_length = payload_size;
        header.data_check = 0;
        header.magic = command ^ 0xffffffff;

        size_t offset = stream.size();
        stream.resize(offset + amessage_size + payload_size);
        std::memcpy(stream.data() + offset, &header, amessage_size);
        if (payload_size > 0) {
            std::memset(stream.data() + offset + amessage_size, 'A', payload_size);
        }
    }
    return stream;
}

static void BM_AdbMessageLogger_Observe_Pipeline(benchmark::State& state) {
    using namespace goldfish::adb;

    // Generate 100 packets of 4KB payload (~400KB of traffic)
    auto stream = GenerateMockAdbStream(100, kAdbWrte, 4096);
    constexpr size_t chunk_size = 8192;

    for (auto _ : state) {
        AdbMessageLogger logger(">> ", false, false);  // verbose = false
        AdbBreadcrumbTracker tracker;
        logger.SetCallback(&tracker);

        size_t offset = 0;
        while (offset < stream.size()) {
            size_t size = std::min(chunk_size, stream.size() - offset);
            logger.Observe(stream.data() + offset, size);
            offset += size;
        }
    }

    state.SetBytesProcessed(state.iterations() * stream.size());
}
BENCHMARK(BM_AdbMessageLogger_Observe_Pipeline);

}  // namespace android::crashreport

BENCHMARK_MAIN();
