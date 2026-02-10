/* Copyright (C) 2025 The Android Open Source Project
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <benchmark/benchmark.h>

#include <atomic>
#include <thread>

#include "goldfish/blocking_stream_buf.h"
#include "goldfish/synchronized_stream_buf.h"

namespace goldfish {

// --- StringBuf Baseline ---
// These go fast!

static void BM_StringBuf_Write(benchmark::State& state) {
    std::string data(state.range(0), 'x');
    for (auto _ : state) {
        std::stringbuf buf;
        std::ostream os(&buf);
        os.write(data.data(), data.size());
        benchmark::DoNotOptimize(buf.str());
    }
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(state.range(0)));
}
BENCHMARK(BM_StringBuf_Write)->Range(64, 8 << 10);

static void BM_StringBuf_Read(benchmark::State& state) {
    std::string data(state.range(0), 'x');
    for (auto _ : state) {
        std::stringbuf buf(data);
        std::istream is(&buf);
        std::string sink(state.range(0), ' ');
        is.read(sink.data(), sink.size());
        benchmark::DoNotOptimize(sink);
    }
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(state.range(0)));
}
BENCHMARK(BM_StringBuf_Read)->Range(64, 8 << 10);

// --- BlockingStreamBuf Benchmarks ---

static void BM_BlockingStreamBuf_Write_NoReader(benchmark::State& state) {
    std::string data(state.range(0), 'x');
    for (auto _ : state) {
        BlockingStreamBuf<char> buf;
        std::ostream os(&buf);
        os.write(data.data(), data.size());
        benchmark::DoNotOptimize(buf);
    }
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(state.range(0)));
}
BENCHMARK(BM_BlockingStreamBuf_Write_NoReader)->Range(64, 8 << 10);

static void BM_BlockingStreamBuf_Read(benchmark::State& state) {
    std::string data(state.range(0), 'x');
    for (auto _ : state) {
        BlockingStreamBuf<char> buf;
        std::thread producer([&]() {
            std::ostream os(&buf);
            os.write(data.data(), data.size());
            buf.Close();
        });

        std::istream is(&buf);
        std::string sink(state.range(0), ' ');
        is.read(sink.data(), sink.size());
        benchmark::DoNotOptimize(sink);
        producer.join();
    }
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(state.range(0)));
}
BENCHMARK(BM_BlockingStreamBuf_Read)->Range(64, 8 << 10);

static void BM_BlockingStreamBuf_Write(benchmark::State& state) {
    BlockingStreamBuf<char> buf;
    std::ostream os(&buf);
    std::string data(state.range(0), 'x');

    std::atomic_bool stop = false;
    std::thread drainer([&]() {
        std::istream is(&buf);
        char buffer[1024];
        while (!stop) {
            is.read(buffer, sizeof(buffer));
        }
    });

    for (auto _ : state) {
        os.write(data.data(), data.size());
    }

    buf.Close();
    stop = true;
    drainer.join();

    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(state.range(0)));
}
BENCHMARK(BM_BlockingStreamBuf_Write)->Range(64, 8 << 10);

// --- SynchronizedStreamBuf Benchmarks ---

static void BM_SynchronizedStreamBuf_Write_NoReader(benchmark::State& state) {
    std::string data(state.range(0), 'x');
    for (auto _ : state) {
        std::stringbuf inner;
        SynchronizedStreamBuf<char> sync_buf(&inner);
        std::ostream os(&sync_buf);
        os.write(data.data(), data.size());
        benchmark::DoNotOptimize(inner.str());
    }
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(state.range(0)));
}
BENCHMARK(BM_SynchronizedStreamBuf_Write_NoReader)->Range(64, 8 << 10);

static void BM_SynchronizedStreamBuf_Read(benchmark::State& state) {
    std::string data(state.range(0), 'x');
    for (auto _ : state) {
        std::stringbuf inner;
        SynchronizedStreamBuf<char> sync_buf(&inner);
        std::thread producer([&]() {
            std::ostream os(&sync_buf);
            os.write(data.data(), data.size());
        });

        std::istream is(&sync_buf);
        std::string sink(state.range(0), ' ');
        // Note: std::stringbuf doesn't block, so we might need to retry or
        // accept that this tests contention more than throughput if data
        // isn't there yet.
        while (is.read(sink.data(), sink.size()).gcount() < (std::streamsize)data.size()) {
            std::this_thread::yield();
            is.clear();
        }
        benchmark::DoNotOptimize(sink);
        producer.join();
    }
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(state.range(0)));
}
BENCHMARK(BM_SynchronizedStreamBuf_Read)->Range(64, 8 << 10);

static void BM_SynchronizedStreamBuf_Write(benchmark::State& state) {
    std::stringbuf inner;
    SynchronizedStreamBuf<char> sync_buf(&inner);
    std::ostream os(&sync_buf);
    std::string data(state.range(0), 'x');

    std::atomic_bool stop = false;
    std::thread drainer([&]() {
        std::istream is(&sync_buf);
        char buffer[1024];
        while (!stop) {
            is.read(buffer, sizeof(buffer));
            is.clear();
            std::this_thread::yield();
        }
    });

    for (auto _ : state) {
        os.write(data.data(), data.size());
    }

    stop = true;
    drainer.join();

    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(state.range(0)));
}
BENCHMARK(BM_SynchronizedStreamBuf_Write)->Range(64, 8 << 10);

static void BM_SynchronizedStreamBuf_ConcurrentWrite(benchmark::State& state) {
    std::string data(state.range(0), 'x');
    std::stringbuf inner;
    SynchronizedStreamBuf<char> sync_buf(&inner);
    std::ostream os(&sync_buf);

    for (auto _ : state) {
        state.PauseTiming();
        inner.str("");
        std::vector<std::thread> threads;
        state.ResumeTiming();

        for (int i = 0; i < 4; ++i) {
            threads.emplace_back([&os, &data]() { os.write(data.data(), data.size()); });
        }
        for (auto& t : threads) {
            t.join();
        }
    }
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(state.range(0)) * 4);
}
BENCHMARK(BM_SynchronizedStreamBuf_ConcurrentWrite)->Range(64, 8 << 10);

}  // namespace goldfish

BENCHMARK_MAIN();
