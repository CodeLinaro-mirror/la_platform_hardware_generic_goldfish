#include <sstream>

#include "absl/log/log.h"
#include "benchmark/benchmark.h"

#include "android/base/logging/AbseilLogBridge.h"

static void BM_ALOGV(benchmark::State& state) {
    int i = 0;
    for (auto s : state) {
        ALOGV(1, "%s:%d", "Counter", i++);
    }
}

static void BM_VLOG(benchmark::State& state) {
    int i = 0;
    for (auto s : state) {
        VLOG(1) << "Counter:" << i++;
    }
}

static void BM_ALOGI(benchmark::State& state) {
    int i = 0;
    for (auto s : state) {
        ALOGI("%s:%d", "Counter", i++);
    }
}

static void BM_LOGI(benchmark::State& state) {
    int i = 0;
    for (auto s : state) {
        LOG(INFO) << "Counter:" << i++;
    }
}

// Apple M1 Pro 2021
// BM_VLOG          7.48 ns         7.48 ns     93369436

// Old: BM_ALOGV          109 ns          109 ns      6441698
// New: BM_ALOGV         9.13 ns         9.10 ns     72448019
// 109 / 9.10 = 11.978021978021978
// 109 / 9.13 = 11.938663745892662

// C++ vs C: 7.48 / 9.13 = 0.81

// Note stdio is active!
// Old BM_ALOGI        10002 ns         4553 ns       150504
// New BM_ALOGI         9640 ns         4564 ns       133379
BENCHMARK(BM_ALOGV);
BENCHMARK(BM_VLOG);
BENCHMARK(BM_ALOGI);
BENCHMARK(BM_LOGI);
BENCHMARK_MAIN();
