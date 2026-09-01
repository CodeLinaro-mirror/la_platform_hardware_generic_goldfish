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
        (void)loop_->Post([]() { benchmark::DoNotOptimize(0); }, context);
    }
}

// Compares std::optional stack initialization and emplace
static void BM_Optional_Emplace(benchmark::State& state) {
    for (auto _ : state) {
        std::optional<int> result;
        result.emplace(42);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_Optional_Emplace);

// Compares initializing absl::StatusOr with a dynamic absl::InternalError (heap alloc per call)
static void BM_StatusOr_DynamicError(benchmark::State& state) {
    for (auto _ : state) {
        absl::StatusOr<int> result = absl::InternalError("Task was not executed on the event loop");
        result = 42;
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_StatusOr_DynamicError);

// Compares initializing absl::StatusOr with a static absl::Status (constructed once, refcount only)
static void BM_StatusOr_StaticError(benchmark::State& state) {
    static const absl::Status* const kDefaultTaskFailure =
            new absl::Status(absl::InternalError("Task was not executed on the event loop"));
    for (auto _ : state) {
        absl::StatusOr<int> result = *kDefaultTaskFailure;
        result = 42;
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_StatusOr_StaticError);

// Compares pure absl::Notification (no StatusOr)
static void BM_Notification_Pure(benchmark::State& state) {
    for (auto _ : state) {
        absl::Notification done;
        done.Notify();
        done.WaitForNotification();
        benchmark::DoNotOptimize(done);
    }
}
BENCHMARK(BM_Notification_Pure);

// Compares absl::Notification + std::optional<int>
static void BM_Notification_With_Optional(benchmark::State& state) {
    for (auto _ : state) {
        absl::Notification done;
        std::optional<int> result;
        result.emplace(42);
        done.Notify();
        done.WaitForNotification();
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_Notification_With_Optional);

// Compares std::promise / std::future creation and resolution overhead (single thread)
static void BM_PromiseFuture_Overhead(benchmark::State& state) {
    for (auto _ : state) {
        std::promise<int> p;
        std::future<int> f = p.get_future();
        p.set_value(42);
        int val = f.get();
        benchmark::DoNotOptimize(val);
    }
}
BENCHMARK(BM_PromiseFuture_Overhead);

// Compares 2-thread handoff: Thread A notifies, Thread B waits
static void BM_Notification_2ThreadHandoff(benchmark::State& state) {
    for (auto _ : state) {
        absl::Notification done;
        std::thread t([&]() { done.Notify(); });
        done.WaitForNotification();
        t.join();
    }
}
BENCHMARK(BM_Notification_2ThreadHandoff);

// Compares 2-thread handoff: Thread A sets promise, Thread B calls future.get()
static void BM_PromiseFuture_2ThreadHandoff(benchmark::State& state) {
    for (auto _ : state) {
        std::promise<void> p;
        std::future<void> f = p.get_future();
        std::thread t([&]() { p.set_value(); });
        f.get();
        t.join();
    }
}
BENCHMARK(BM_PromiseFuture_2ThreadHandoff);

// Benchmarks end-to-end PostAndWait roundtrip throughput on a running event loop
static void BM_EventLoop_PostAndWait(benchmark::State& state) {
    auto loop = LibuvEventLoop::Create();
    std::thread loop_thread([&]() { loop->Run().IgnoreError(); });

    while (loop->GetState() != LooperStatusEvent::State::kRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    for (auto _ : state) {
        auto res = loop->PostAndWait([]() { return 42; });
        benchmark::DoNotOptimize(res);
    }

    loop->ShutdownAndWait().IgnoreError();
    if (loop_thread.joinable()) {
        loop_thread.join();
    }
}
BENCHMARK(BM_EventLoop_PostAndWait);

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

// Benchmarks value-returning Post tasks requiring std::promise / std::future allocation.
static void BM_EventLoop_ValueReturning_PipelineThroughput(benchmark::State& state) {
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
            auto fut = loop->Post([&]() -> int {
                counter.DecrementCount();
                return 42;
            });
            benchmark::DoNotOptimize(fut);
        }
        counter.Wait();
    }

    loop->ShutdownAndWait().IgnoreError();
    if (loop_thread.joinable()) {
        loop_thread.join();
    }
}
BENCHMARK(BM_EventLoop_ValueReturning_PipelineThroughput);

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
