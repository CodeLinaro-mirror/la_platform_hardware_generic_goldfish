// Copyright 2025 The Android Open Source Project
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

#include <benchmark/benchmark.h>

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

#include "grpc_diagnostic.pb.h"

#include "goldfish/circular_message_log.h"
#include "goldfish/raw_circular_log.h"

namespace goldfish::proto_data_store {
namespace {

using android::control::interceptor::GrpcBreadcrumb;

void BmRawPush(benchmark::State& state) {
    std::vector<char> buffer(static_cast<size_t>(64) * 1024);
    auto log = *RawCircularLog::CreateWriter(buffer.data(), buffer.size());

    constexpr uint32_t kSize = 32;
    char data[kSize];
    std::memset(data, 'X', kSize);

    for (auto _ : state) {
        benchmark::DoNotOptimize(
                log->Push(kSize, [&](void* dest) { std::memcpy(dest, data, kSize); }));
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(kSize));
}

void BmProtoPush(benchmark::State& state) {
    std::vector<char> buffer(static_cast<size_t>(64) * 1024);
    auto log = *ProtoCircularLog<GrpcBreadcrumb>::CreateWriter(buffer.data(), buffer.size());

    GrpcBreadcrumb crumb;
    crumb.set_call_id(1);
    crumb.set_payload(std::string(20, 'P'));

    const size_t msg_size = crumb.ByteSizeLong();

    for (auto _ : state) {
        benchmark::DoNotOptimize(log->Push(crumb));
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(msg_size));
}

void BmConcurrentProtoPush(benchmark::State& state) {
    static const auto kBuffer =
            std::make_unique<std::vector<char>>(static_cast<size_t>(4096) * 1024);
    static const auto kLog =
            ProtoCircularLog<GrpcBreadcrumb>::CreateWriter(kBuffer->data(), kBuffer->size())
                    .value();

    GrpcBreadcrumb crumb;
    crumb.set_call_id(1);

    const size_t msg_size = crumb.ByteSizeLong();

    for (auto _ : state) {
        benchmark::DoNotOptimize(kLog->Push(crumb));
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(msg_size));
}

void BmForEach(benchmark::State& state) {
    std::vector<char> buffer(static_cast<size_t>(64) * 1024);
    auto log = *ProtoCircularLog<GrpcBreadcrumb>::CreateWriter(buffer.data(), buffer.size());

    GrpcBreadcrumb crumb;
    for (int i = 0; i < 100; ++i) {
        crumb.set_call_id(i);
        (void)log->Push(crumb);
    }

    for (auto _ : state) {
        log->ForEach([](const GrpcBreadcrumb& m) {
            benchmark::DoNotOptimize(&m);
            return true;
        });
    }
}

}  // namespace

BENCHMARK(BmRawPush);
BENCHMARK(BmProtoPush);
BENCHMARK(BmConcurrentProtoPush)->ThreadRange(1, 16);
BENCHMARK(BmForEach);

}  // namespace goldfish::proto_data_store

BENCHMARK_MAIN();
