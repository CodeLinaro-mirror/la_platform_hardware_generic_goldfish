// Copyright (C) 2025 The Android Open Source Project
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
#include <memory>

#include "absl/time/clock.h"

#include "android/base/abseil_clock.h"
#include "android/base/clock.h"

namespace android::base {
namespace {

// --- Baseline ---
// This simulates the "ideal" compile-time approach: a direct, non-virtual,
// non-singleton function call. This gives us a performance baseline.
static void BM_DirectCall(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(absl::Now());
    }
}
BENCHMARK(BM_DirectCall);

// --- Mutex Version (Current Implementation) ---
// This benchmarks the current IClock::get() implementation, which uses a
// std::mutex to protect a static unique_ptr.
static void BM_MutexClock(benchmark::State& state) {
    // In a multi-threaded benchmark, this setup runs once per thread.
    // We only need to set the clock once for the entire benchmark.
    if (state.thread_index() == 0) {
        IClock::set(std::make_unique<AbseilClock>());
    }

    for (auto _ : state) {
        benchmark::DoNotOptimize(IClock::get().now(ClockType::Host));
    }
}
// Run this benchmark with 1, 2, 4, and 8 threads to see how the mutex
// performs under contention.
BENCHMARK(BM_MutexClock)->ThreadRange(1, 8);

// --- Atomic Version (Hypothetical) ---
// This section implements a simplified, lock-free version of the clock
// accessor for performance comparison.
// WARNING: As discussed in Design.md, this approach has serious lifetime
// management issues and is NOT suitable for production code. It is included
// here only for the purpose of the benchmark.

namespace atomic_clock {

// A default instance, created using the thread-safe "magic static" idiom.
IClock& getDefaultClock() {
    static AbseilClock sDefaultInstance;
    return sDefaultInstance;
}

// The global atomic pointer to the current clock instance.
std::atomic<IClock*> gInstance{nullptr};

// The lock-free getter.
IClock& get() {
    IClock* ptr = gInstance.load(std::memory_order_acquire);
    if (ptr == nullptr) {
        return getDefaultClock();
    }
    return *ptr;
}

// A setter for the atomic pointer.
// NOTE: This leaks the previously set pointer!
void set(IClock* clock) {
    gInstance.store(clock, std::memory_order_release);
}

}  // namespace atomic_clock

static void BM_AtomicClock(benchmark::State& state) {
    if (state.thread_index() == 0) {
        // Note: This leaks memory, which is why this pattern is not used in
        // production. The OS will clean it up when the benchmark process exits.
        atomic_clock::set(new AbseilClock());
    }

    for (auto _ : state) {
        benchmark::DoNotOptimize(atomic_clock::get().now(ClockType::Host));
    }
}
BENCHMARK(BM_AtomicClock)->ThreadRange(1, 8);

}  // namespace
}  // namespace android::base

BENCHMARK_MAIN();
