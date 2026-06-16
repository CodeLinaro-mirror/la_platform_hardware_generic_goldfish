#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/synchronization/blocking_counter.h"

#include "android/crashreport/breadcrumb_proto.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/raw_circular_log.h"

namespace goldfish::async {

using ::android::crashreport::RawLooperPostWithContextPayload;

namespace {

class EventLoopBenchmark : public ::benchmark::Fixture {
  public:
    void SetUp(const ::benchmark::State& state) override { loop_ = LibuvEventLoop::Create(); }

    void TearDown(const ::benchmark::State& state) override { loop_.reset(); }

  protected:
    std::unique_ptr<LibuvEventLoop> loop_;
};

// Benchmarks the raw task posting throughput (no worker thread executing).
// This measures the overhead of Post() itself (generating flow_id, logging FLOW_BEGIN).
BENCHMARK_F(EventLoopBenchmark, PostThroughput)(benchmark::State& state) {
    // We do not start the loop thread, so tasks just accumulate in the queue.
    for (auto _ : state) {
        (void)loop_->Post([]() { benchmark::DoNotOptimize(0); });
    }
}

// Benchmarks the raw task posting throughput with a dynamic context string.
BENCHMARK_F(EventLoopBenchmark, PostWithContextThroughput)(benchmark::State& state) {
    std::string_view context = "sensor-data-packet-32b-length";
    for (auto _ : state) {
        // Under baseline compilation, this overload may fall back to standard Post.
        // Once implemented, it will measure the stack-alloc and memcpy overhead.
        (void)loop_->Post([]() { benchmark::DoNotOptimize(0); }, std::chrono::milliseconds::zero(),
                          context);
    }
}

// Benchmarks the concurrent pipeline throughput (Post + Execution running in parallel).
// This is the most realistic performance test.
static void BM_EventLoop_PipelineThroughput(benchmark::State& state) {
    auto loop = LibuvEventLoop::Create();
    std::thread loop_thread([&]() { loop->Run().IgnoreError(); });

    // Wait for loop to start
    while (loop->GetState() != LooperStatusEvent::State::kRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const int batch_size = 500;

    for (auto _ : state) {
        absl::BlockingCounter counter(batch_size);
        for (int i = 0; i < batch_size; ++i) {
            (void)loop->Post([&]() { counter.DecrementCount(); });
        }
        counter.Wait();
    }

    loop->ShutdownAndWait().IgnoreError();
    if (loop_thread.joinable()) {
        loop_thread.join();
    }
}
BENCHMARK(BM_EventLoop_PipelineThroughput);

// Benchmarks multi-threaded queue contention on the looper.
static void BM_EventLoop_ConcurrentContention(benchmark::State& state) {
    auto loop = LibuvEventLoop::Create();
    std::thread loop_thread([&]() { loop->Run().IgnoreError(); });

    while (loop->GetState() != LooperStatusEvent::State::kRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const int num_threads = state.range(0);
    const int batch_size = 100;

    for (auto _ : state) {
        std::vector<std::thread> workers;
        absl::BlockingCounter counter(num_threads * batch_size);

        for (int t = 0; t < num_threads; ++t) {
            workers.emplace_back([&]() {
                for (int i = 0; i < batch_size; ++i) {
                    (void)loop->Post([&]() { counter.DecrementCount(); });
                }
            });
        }

        for (auto& w : workers) {
            w.join();
        }
        counter.Wait();
    }

    loop->ShutdownAndWait().IgnoreError();
    if (loop_thread.joinable()) {
        loop_thread.join();
    }
}
BENCHMARK(BM_EventLoop_ConcurrentContention)->Arg(2)->Arg(4)->Arg(8);

// Simulates Option A: Multiple threads writing to a single global circular log.
static void BM_Contention_GlobalLog(benchmark::State& state) {
    static void* buffer = ::malloc(16384);
    static auto global_log =
            goldfish::proto_data_store::RawCircularLog::CreateWriter(buffer, 16384).value();

    for (auto _ : state) {
        RawLooperPostWithContextPayload payload{
            .caller_pc = 0x1234, .loop_id = 1, .context_len = 0};
        global_log
                ->Push(sizeof(payload),
                       [&](void* dest) { std::memcpy(dest, &payload, sizeof(payload)); })
                .IgnoreError();
    }
}
BENCHMARK(BM_Contention_GlobalLog)->ThreadRange(1, 8);

// Simulates Option B (Option 1): Multiple threads writing to loop-local circular logs.
static void BM_Contention_LocalLogs(benchmark::State& state) {
    // Create local logs lazily
    static std::vector<void*> buffers = []() {
        std::vector<void*> b;
        for (int i = 0; i < 64; ++i) b.push_back(::malloc(4096));
        return b;
    }();
    static std::vector<std::unique_ptr<goldfish::proto_data_store::RawCircularLog>> local_logs =
            []() {
                std::vector<std::unique_ptr<goldfish::proto_data_store::RawCircularLog>> logs;
                for (int i = 0; i < 64; ++i) {
                    logs.push_back(goldfish::proto_data_store::RawCircularLog::CreateWriter(
                                           buffers[i], 4096)
                                           .value());
                }
                return logs;
            }();

    for (auto _ : state) {
        struct LocalPayload {
            uint64_t caller_pc;
        } payload{.caller_pc = 0x1234};
        auto& log = local_logs[state.thread_index()];
        log->Push(sizeof(payload), [&](void* dest) {
               std::memcpy(dest, &payload, sizeof(payload));
           }).IgnoreError();
    }
}
BENCHMARK(BM_Contention_LocalLogs)->ThreadRange(1, 8);

}  // namespace
}  // namespace goldfish::async

BENCHMARK_MAIN();
