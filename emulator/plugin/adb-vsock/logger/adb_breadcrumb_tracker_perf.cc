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

/*
 * Performance progression and improvements for the ADB logging pipeline.
 * Measured using BM_AdbMessageLogger_Observe_Pipeline (parsing 100 packets
 * with 4KB payloads in 8KB chunks):
 *
 * | Phase | Latency (Per 100 Packets) | Throughput | Improvement |
 * | :--- | :---: | :---: | :---: |
 * | 1. Baseline (Unoptimized Proto) | 20,309 ns | 18.91 Gi/s | — |
 * | 2. Logger Optimized (Proto)      | 16,346 ns | 23.49 Gi/s | +24% |
 * | 3. Binary Refactor (Unoptimized) | 13,025 ns | 29.46 Gi/s | +55% |
 * | 4. Tracker Optimized (Binary)    | 11,203 ns | 34.25 Gi/s | +81% |
 *
 * Optimizations applied:
 * - Logger: Avoided resizing/copying payload to packet_buffer_ when verbose logging is disabled.
 * - Tracker: Replaced std::vector heap allocation with a fixed 64-byte stack buffer for
 * RawAdbPayload serialization.
 */

#include <benchmark/benchmark.h>

#include <cstring>
#include <string>
#include <vector>

#include "goldfish/adb/adb_breadcrumb_tracker.h"
#include "goldfish/adb/adb_message_logger.h"

namespace goldfish::adb {

void SetupStream(AdbBreadcrumbTracker& tracker, uint32_t host_id, uint32_t guest_id,
                 const std::string& service) {
    AMessage open_msg;
    open_msg.command = 0x4E45504F;  // 'OPEN'
    open_msg.arg0 = host_id;
    open_msg.arg1 = 0;
    open_msg.data_length = service.size() + 1;
    tracker.OnPacket(open_msg, service.c_str(), true);

    AMessage okay_msg;
    okay_msg.command = 0x59414B4F;  // 'OKAY'
    okay_msg.arg0 = guest_id;
    okay_msg.arg1 = host_id;
    okay_msg.data_length = 0;
    tracker.OnPacket(okay_msg, nullptr, false);
}

void BmAdbTrackerOnPacket(benchmark::State& state) {
    AdbBreadcrumbTracker tracker;
    AMessage message;
    message.command = 0x4E584E43;  // 'CNXN'
    message.arg0 = 1;
    message.arg1 = 2;
    message.data_length = 0;
    message.magic = message.command ^ 0xffffffff;

    for (auto _ : state) {
        tracker.OnPacket(message, nullptr, true);
    }
}
BENCHMARK(BmAdbTrackerOnPacket);

void BmAdbTrackerOnPacketWrte(benchmark::State& state) {
    AdbBreadcrumbTracker tracker;
    AMessage message;
    message.command = 0x45545257;  // 'WRTE'
    message.arg0 = 1;
    message.arg1 = 2;
    message.data_length = 32;
    message.magic = message.command ^ 0xffffffff;

    std::vector<char> data(32, 'X');

    for (auto _ : state) {
        tracker.OnPacket(message, data.data(), true);
    }
}
BENCHMARK(BmAdbTrackerOnPacketWrte);

void BmAdbTrackerOnPacketWrteSync(benchmark::State& state) {
    AdbBreadcrumbTracker tracker;
    SetupStream(tracker, 1, 2, "sync:");

    AMessage message;
    message.command = 0x45545257;  // 'WRTE'
    message.arg0 = 1;
    message.arg1 = 2;
    message.data_length = 32;
    message.magic = message.command ^ 0xffffffff;

    std::vector<char> data(32, 'X');
    std::memcpy(data.data(), "STAT", 4);

    for (auto _ : state) {
        tracker.OnPacket(message, data.data(), true);
    }
}
BENCHMARK(BmAdbTrackerOnPacketWrteSync);

void BmAdbPullSteadyState(benchmark::State& state) {
    AdbBreadcrumbTracker tracker;
    uint32_t host_id = 1;
    uint32_t guest_id = 2;
    SetupStream(tracker, host_id, guest_id, "sync:");

    AMessage wrte_msg;
    wrte_msg.command = 0x45545257;  // 'WRTE'
    wrte_msg.arg0 = guest_id;
    wrte_msg.arg1 = host_id;
    wrte_msg.data_length = 64 * 1024;
    wrte_msg.magic = wrte_msg.command ^ 0xffffffff;

    std::vector<char> data(64 * 1024, 'X');
    std::memcpy(data.data(), "DATA", 4);

    AMessage okay_msg;
    okay_msg.command = 0x59414B4F;  // 'OKAY'
    okay_msg.arg0 = host_id;
    okay_msg.arg1 = guest_id;
    okay_msg.data_length = 0;
    okay_msg.magic = okay_msg.command ^ 0xffffffff;

    for (auto _ : state) {
        tracker.OnPacket(wrte_msg, data.data(), false);
        tracker.OnPacket(okay_msg, nullptr, true);
    }
}
BENCHMARK(BmAdbPullSteadyState);

void BmAdbPushSteadyState(benchmark::State& state) {
    AdbBreadcrumbTracker tracker;
    uint32_t host_id = 1;
    uint32_t guest_id = 2;
    SetupStream(tracker, host_id, guest_id, "sync:");

    AMessage wrte_msg;
    wrte_msg.command = 0x45545257;  // 'WRTE'
    wrte_msg.arg0 = host_id;
    wrte_msg.arg1 = guest_id;
    wrte_msg.data_length = 64 * 1024;
    wrte_msg.magic = wrte_msg.command ^ 0xffffffff;

    std::vector<char> data(64 * 1024, 'X');
    std::memcpy(data.data(), "DATA", 4);

    AMessage okay_msg;
    okay_msg.command = 0x59414B4F;  // 'OKAY'
    okay_msg.arg0 = guest_id;
    okay_msg.arg1 = host_id;
    okay_msg.data_length = 0;
    okay_msg.magic = okay_msg.command ^ 0xffffffff;

    for (auto _ : state) {
        tracker.OnPacket(wrte_msg, data.data(), true);
        tracker.OnPacket(okay_msg, nullptr, false);
    }
}
BENCHMARK(BmAdbPushSteadyState);

void BmAdbLogcatSteadyState(benchmark::State& state) {
    AdbBreadcrumbTracker tracker;
    uint32_t host_id = 1;
    uint32_t guest_id = 2;
    SetupStream(tracker, host_id, guest_id, "shell:logcat");

    AMessage wrte_msg;
    wrte_msg.command = 0x45545257;  // 'WRTE'
    wrte_msg.arg0 = guest_id;
    wrte_msg.arg1 = host_id;
    wrte_msg.data_length = 120;
    wrte_msg.magic = wrte_msg.command ^ 0xffffffff;

    std::vector<char> data(120, 'X');

    AMessage okay_msg;
    okay_msg.command = 0x59414B4F;  // 'OKAY'
    okay_msg.arg0 = host_id;
    okay_msg.arg1 = guest_id;
    okay_msg.data_length = 0;
    okay_msg.magic = okay_msg.command ^ 0xffffffff;

    for (auto _ : state) {
        tracker.OnPacket(wrte_msg, data.data(), false);
        tracker.OnPacket(okay_msg, nullptr, true);
    }
}
BENCHMARK(BmAdbLogcatSteadyState);

// --- AdbLogger Benchmarks (Parser + Tracker) ---

std::vector<char> GenPacket(uint32_t command, uint32_t arg0, uint32_t arg1, const void* data,
                            uint32_t data_len) {
    std::vector<char> bytes(sizeof(AMessage) + data_len);
    AMessage* msg = reinterpret_cast<AMessage*>(bytes.data());
    msg->command = command;
    msg->arg0 = arg0;
    msg->arg1 = arg1;
    msg->data_length = data_len;
    msg->data_check = 0;
    msg->magic = command ^ 0xffffffff;
    if (data_len > 0 && data != nullptr) {
        std::memcpy(bytes.data() + sizeof(AMessage), data, data_len);
    }
    return bytes;
}

void BmAdbLoggerPull(benchmark::State& state) {
    AdbLogger logger(1, 2, false);

    auto open_bytes = GenPacket(0x4E45504F, 1, 0, "sync:", 6);
    logger.ToSocket(open_bytes.data(), open_bytes.size());

    auto okay_bytes = GenPacket(0x59414B4F, 2, 1, nullptr, 0);
    logger.ToPlug(okay_bytes.data(), okay_bytes.size());

    std::vector<char> payload(64 * 1024, 'X');
    std::memcpy(payload.data(), "DATA", 4);
    auto wrte_bytes = GenPacket(0x45545257, 2, 1, payload.data(), payload.size());
    auto okay_ack = GenPacket(0x59414B4F, 1, 2, nullptr, 0);

    for (auto _ : state) {
        logger.ToPlug(wrte_bytes.data(), wrte_bytes.size());
        logger.ToSocket(okay_ack.data(), okay_ack.size());
    }
}
BENCHMARK(BmAdbLoggerPull);

void BmAdbLoggerPush(benchmark::State& state) {
    AdbLogger logger(1, 2, false);

    auto open_bytes = GenPacket(0x4E45504F, 1, 0, "sync:", 6);
    logger.ToSocket(open_bytes.data(), open_bytes.size());

    auto okay_bytes = GenPacket(0x59414B4F, 2, 1, nullptr, 0);
    logger.ToPlug(okay_bytes.data(), okay_bytes.size());

    std::vector<char> payload(64 * 1024, 'X');
    std::memcpy(payload.data(), "DATA", 4);
    auto wrte_bytes = GenPacket(0x45545257, 1, 2, payload.data(), payload.size());
    auto okay_ack = GenPacket(0x59414B4F, 2, 1, nullptr, 0);

    for (auto _ : state) {
        logger.ToSocket(wrte_bytes.data(), wrte_bytes.size());
        logger.ToPlug(okay_ack.data(), okay_ack.size());
    }
}
BENCHMARK(BmAdbLoggerPush);

void BmAdbLoggerLogcat(benchmark::State& state) {
    AdbLogger logger(1, 2, false);

    auto open_bytes = GenPacket(0x4E45504F, 1, 0, "shell:logcat", 13);
    logger.ToSocket(open_bytes.data(), open_bytes.size());

    auto okay_bytes = GenPacket(0x59414B4F, 2, 1, nullptr, 0);
    logger.ToPlug(okay_bytes.data(), okay_bytes.size());

    std::vector<char> payload(120, 'X');
    auto wrte_bytes = GenPacket(0x45545257, 2, 1, payload.data(), payload.size());
    auto okay_ack = GenPacket(0x59414B4F, 1, 2, nullptr, 0);

    for (auto _ : state) {
        logger.ToPlug(wrte_bytes.data(), wrte_bytes.size());
        logger.ToSocket(okay_ack.data(), okay_ack.size());
    }
}
BENCHMARK(BmAdbLoggerLogcat);

void BmAdbTrackerStreamLifecycle(benchmark::State& state) {
    AdbBreadcrumbTracker tracker;
    uint32_t id = 1;

    for (auto _ : state) {
        // OPEN (Host to Guest)
        AMessage open_msg;
        open_msg.command = 0x4E45504F;  // 'OPEN'
        open_msg.arg0 = id;
        open_msg.arg1 = 0;
        const char* service = "sync:";
        open_msg.data_length = 6;
        tracker.OnPacket(open_msg, service, true);

        // OKAY (Guest to Host)
        AMessage okay_msg;
        okay_msg.command = 0x59414B4F;  // 'OKAY'
        okay_msg.arg0 = id + 1;
        okay_msg.arg1 = id;
        okay_msg.data_length = 0;
        tracker.OnPacket(okay_msg, nullptr, false);

        // CLSE (Host to Guest)
        AMessage clse_msg;
        clse_msg.command = 0x45534C43;  // 'CLSE'
        clse_msg.arg0 = id;
        clse_msg.arg1 = id + 1;
        clse_msg.data_length = 0;
        tracker.OnPacket(clse_msg, nullptr, true);

        id += 2;
    }
}
BENCHMARK(BmAdbTrackerStreamLifecycle);

AdbBreadcrumbTracker* GetSharedTracker() {
    static AdbBreadcrumbTracker* tracker = []() {
        auto* t = new AdbBreadcrumbTracker();
        SetupStream(*t, 1, 2, "sync:");
        return t;
    }();
    return tracker;
}

void BmAdbTrackerMultiThreadedWrte(benchmark::State& state) {
    auto* tracker = GetSharedTracker();

    AMessage message;
    message.command = 0x45545257;  // 'WRTE'
    message.arg0 = 1;
    message.arg1 = 2;
    message.data_length = 32;
    message.magic = message.command ^ 0xffffffff;

    std::vector<char> data(32, 'X');
    std::memcpy(data.data(), "STAT", 4);

    for (auto _ : state) {
        tracker->OnPacket(message, data.data(), true);
    }
}
BENCHMARK(BmAdbTrackerMultiThreadedWrte)->ThreadRange(1, 8);

}  // namespace goldfish::adb

BENCHMARK_MAIN();
