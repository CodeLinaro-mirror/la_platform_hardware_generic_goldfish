// Copyright 2024 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include <android/base/system/System.h>

#include <string>
#include <thread>

#include "benchmark/benchmark.h"

#include "aemu/base/system/System.h"
#include "android/base/sockets/SimpleTestServer.h"

#ifndef _WIN32

#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using android::base::testing::SimpleTestServer;

// ASYNC SOCKET SERVER PERF

static void BM_ASYNC_SOCKET_PERF(benchmark::State& state) {
    int i = 0;
    SimpleTestServer server;
    for (auto s : state) {
        server.start();
        auto socket = server.connect();
        for (int i = 0; i < 100; i++) {
            std::string msg(state.range(0), 'x');
            socket->send(msg);
        }
        socket->close();
        server.waitUntilCompleted();
    }
}

// THREAD ID PERF
//
static std::thread::id getStdThreadId() {
    return std::this_thread::get_id();
}

static std::thread::id getStdThreadIdCached() {
    // Cache in thread local stack.
    static thread_local std::thread::id thread_id = std::this_thread::get_id();
    return thread_id;
}

static void BM_THREAD_ID_STD(benchmark::State& state) {
    for (auto s : state) {
        benchmark::DoNotOptimize(getStdThreadId());
    }
}

static void BM_THREAD_ID_CACHED_STD(benchmark::State& state) {
    for (auto s : state) {
        benchmark::DoNotOptimize(getStdThreadIdCached());
    }
}

#ifndef _WIN32
static uint64_t getPthreadId() {
    uint64_t tid;
    pthread_threadid_np(NULL, &tid);
    return tid;
}

static void BM_THREAD_ID_SYSTEM(benchmark::State& state) {
    for (auto s : state) {
        benchmark::DoNotOptimize(getPthreadId());
    }
}
#endif

// Old:      BM_ASYNC_SOCKET_PERF     789269 ns        75207 ns         9246 (broken)
//           BM_ASYNC_SOCKET_PERF     449198 ns        77048 ns         8860
BENCHMARK(BM_ASYNC_SOCKET_PERF)->Range(8, 8 << 12);

// ------------------------------------------------------------------
// Benchmark                        Time             CPU   Iterations
// ------------------------------------------------------------------
// BM_THREAD_ID_STD              2.26 ns         2.25 ns    310841715
// BM_THREAD_ID_CACHED_STD       1.38 ns         1.38 ns    508314574 << Winner!
// BM_THREAD_ID_SYSTEM           2.32 ns         2.28 ns    309515387
// -----------------------------------------------------------------------
BENCHMARK(BM_THREAD_ID_STD);
BENCHMARK(BM_THREAD_ID_CACHED_STD);
BENCHMARK(BM_THREAD_ID_SYSTEM);

BENCHMARK_MAIN();
