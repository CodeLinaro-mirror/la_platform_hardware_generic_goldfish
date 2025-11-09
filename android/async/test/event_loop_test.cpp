#include "goldfish/async/event_loop.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <numeric>
#include <queue>
#include <thread>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/status/status_matchers.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"

#include "fake_qemu_callbacks.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/qemu_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"

using namespace std::chrono_literals;

namespace goldfish::async {

// Test fixture parameterized by the event loop type ("libuv" or "qemu").
class EventLoopTest : public ::testing::TestWithParam<std::string> {
  protected:
    void SetUp() override {
        mLoopType = GetParam();
        if (mLoopType == "libuv") {
            mLibuvLoop = LibuvEventLoop::create();
            loop = mLibuvLoop.get();
        } else if (mLoopType == "qemu") {
            mLibuvLoop = QemuEventLoop::create();
            loop = mLibuvLoop.get();
        }
    }

    void TearDown() override {
        shutdown();
        if (mLoopType == "qemu") {
            fake_qemu_reset();
        }
    }

    void shutdown() {
        if (loop) {
            if (loop->getState() == LooperStatusEvent::State::RUNNING) {
                auto f = loop->shutdown(5s);
                if (mLoopType == "qemu") {
                    // Need to run Qemu "thread" for shutdown to complete
                    fake_qemu_advance_ms(150);
                }
                ASSERT_THAT(f.get(), absl_testing::IsOk());
                loop->stop();
            }
            loop = nullptr;
        }
        if (mLoopType == "libuv") {
            if (loop_thread.joinable()) {
                loop_thread.join();
            }
        }
        mLibuvLoop.reset();
    }

    // Starts the libuv event loop in a background thread. No-op for qemu.
    void runInThread() {
        if (mLoopType == "libuv") {
            loop_thread = std::thread([this]() { (void)loop->run(); });
        }
        // Wait for it to actually start.
        while (loop->getState() != LooperStatusEvent::State::RUNNING) {
            std::this_thread::sleep_for(10ms);
        }
    }

    // Processes events until a future is fulfilled.
    template <typename T>
    void runUntil(std::future<T>& future) {
        if (mLoopType == "libuv") {
            ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
        } else if (mLoopType == "qemu") {
            while (future.wait_for(0ms) != std::future_status::ready) {
                fake_qemu_advance_ms(1);
            }
        }
    }

    void runUntil(std::future<void>& future) {
        if (mLoopType == "libuv") {
            ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
        } else if (mLoopType == "qemu") {
            while (future.wait_for(0ms) != std::future_status::ready) {
                fake_qemu_advance_ms(1);
            }
        }
    }

    const std::chrono::milliseconds tolerance = std::chrono::milliseconds(50);
    std::string mLoopType;
    std::unique_ptr<EventLoop> mLibuvLoop;
    EventLoop* loop = nullptr;
    std::thread loop_thread;
};

INSTANTIATE_TEST_SUITE_P(EventLoopImplementations, EventLoopTest,
                         ::testing::Values("libuv", "qemu"),
                         [](const ::testing::TestParamInfo<EventLoopTest::ParamType>& info) {
                             std::string name = info.param;
                             std::transform(name.begin(), name.end(), name.begin(),
                                            [](unsigned char c) { return std::toupper(c); });
                             return name;
                         });

// =================================================================
//                      TEST CASES
// =================================================================

TEST_P(EventLoopTest, ScheduleAndExecuteSingleTask) {
    std::promise<bool> task_executed_promise;
    auto future = task_executed_promise.get_future();

    (void)loop->post([&]() { task_executed_promise.set_value(true); });

    runInThread();
    runUntil(future);

    EXPECT_TRUE(future.get());
}

TEST_P(EventLoopTest, ScheduleAndExecuteSingleTaskWithinATask) {
    std::promise<bool> task_executed_promise;
    auto future = task_executed_promise.get_future();

    (void)loop->post([&]() { (void)loop->post([&]() { task_executed_promise.set_value(true); }); });

    runInThread();
    runUntil(future);

    EXPECT_TRUE(future.get());
}

// From https://github.com/google/googletest/blob/main/docs/advanced.md#death-tests-and-threads
// Due to well-known problems with forking in the presence of threads, death tests should be run in
// a single-threaded context. Sometimes, however, it isn't feasible to arrange that kind of
// environment. For example, statically-initialized modules may start threads before main is ever
// reached. Once threads have been created, it may be difficult or impossible to clean them up.
//
// For us this translates into all sorts of strange behavior.
TEST_P(EventLoopTest, DISABLED_PostAndWaitFromLoopThreadFails) {
    if (mLoopType == "libuv") {
        std::promise<void> status_promise;
        auto status_future = status_promise.get_future();
        runInThread();
        (void)loop->post([this, &status_promise]() {
            EXPECT_DEATH(this->loop->postAndWait([]() { return false; }),
                         "postAndWait cannot be called from the event loop.");
            status_promise.set_value();
        });
        runUntil(status_future);
    } else {  // qemu
        // For qemu, the main test thread is the loop thread.
        EXPECT_DEATH(this->loop->postAndWait([]() { return false; }),
                     "postAndWait cannot be called from the event loop.");
    }
}

TEST_P(EventLoopTest, ScheduleAndExecuteSingleTaskOnRunningLoop) {
    if (mLoopType == "qemu") {
        GTEST_SKIP() << "ThreadedEventLoop is not compatible with the singleton QemuEventLoop.";
    }
    std::promise<bool> task_executed_promise;
    auto future = task_executed_promise.get_future();
    loop = nullptr;
    auto running_loop = ThreadedEventLoop::create(std::move(mLibuvLoop));

    (void)running_loop->post([&]() { task_executed_promise.set_value(true); });

    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
    EXPECT_TRUE(future.get());
}

TEST_P(EventLoopTest, TasksExecuteInScheduledOrder) {
    std::promise<std::vector<int>> results_promise;
    auto future = results_promise.get_future();

    std::vector<int> results;
    const int task_count = 100;

    for (int i = 0; i < task_count; ++i) {
        (void)loop->post([&, i]() { results.push_back(i); });
    }
    (void)loop->post([&]() { results_promise.set_value(results); });

    runInThread();
    runUntil(future);

    std::vector<int> final_results = future.get();
    std::vector<int> expected_results(task_count);
    std::iota(expected_results.begin(), expected_results.end(), 0);

    EXPECT_EQ(final_results, expected_results);
}

TEST_P(EventLoopTest, IsOnLoopThreadIsCorrect) {
    std::promise<bool> on_loop_thread_promise;
    auto future = on_loop_thread_promise.get_future();

    if (mLoopType == "libuv") {
        EXPECT_FALSE(loop->isOnLoopThread());
        runInThread();
    } else {  // qemu now has a separate thread runner.
        EXPECT_FALSE(loop->isOnLoopThread());
    }

    (void)loop->post([&]() { on_loop_thread_promise.set_value(loop->isOnLoopThread()); });
    runUntil(future);
    EXPECT_TRUE(future.get());

    // Check from another thread
    std::promise<bool> other_thread_promise;
    auto other_future = other_thread_promise.get_future();
    std::thread other_thread([&]() { other_thread_promise.set_value(loop->isOnLoopThread()); });
    other_thread.join();
    EXPECT_FALSE(other_future.get());
}

TEST_P(EventLoopTest, DISABLED_RunExitsWhenStopped) {
    // This is improper use of the event loop, and will result in memory leaks
    // therefore it is disabled (it did expose a deadlock, of calling shutdown
    // adfter stop)
    if (mLoopType == "qemu") {
        GTEST_SKIP() << "QemuEventLoop does not support run() or stop().";
    }
    runInThread();
    // Give the thread a moment to start and enter run().
    std::this_thread::sleep_for(20ms);
    loop->stop();
    // The fixture's TearDown will join the thread.
}

TEST_P(EventLoopTest, CanScheduleTaskFromAnotherThread) {
    std::promise<bool> task_ran_promise;
    auto future = task_ran_promise.get_future();

    runInThread();

    std::thread worker_thread(
            [&]() { (void)loop->post([&]() { task_ran_promise.set_value(true); }); });

    runUntil(future);
    EXPECT_TRUE(future.get());

    worker_thread.join();
}

TEST_P(EventLoopTest, CanScheduleTaskFromWithinAnotherTask) {
    std::promise<int> final_task_promise;
    auto future = final_task_promise.get_future();

    (void)loop->post([&]() { (void)loop->post([&]() { final_task_promise.set_value(42); }); });

    runInThread();
    runUntil(future);
    EXPECT_EQ(future.get(), 42);
}

TEST_P(EventLoopTest, MassConcurrencyPost) {
    if (mLoopType == "qemu") {
        GTEST_SKIP() << "QemuEventLoop stubs do not support this scenario.";
    }
    const int num_threads = 8;
    const int tasks_per_thread = 1000;
    const int total_tasks = num_threads * tasks_per_thread;
    absl::BlockingCounter counter(total_tasks);

    // A shared counter to track completed tasks on the event loop thread.
    std::atomic<int> completed_tasks_count = 0;

    // Start the event loop in its own thread.
    std::thread loop_thread([&]() { (void)loop->run(); });

    // Create multiple threads, each posting tasks.
    std::vector<std::thread> worker_threads;
    for (int i = 0; i < num_threads; ++i) {
        worker_threads.emplace_back([&]() {
            for (int j = 0; j < tasks_per_thread; ++j) {
                (void)loop->post([&]() {
                    completed_tasks_count++;
                    counter.DecrementCount();
                });
            }
        });
    }

    // Wait for all tasks to be executed.
    counter.Wait();

    // Verify that the final count is correct.
    EXPECT_EQ(completed_tasks_count, total_tasks);

    // Clean up.
    loop->shutdown(1s).wait();
    loop->stop();
    for (auto& t : worker_threads) {
        t.join();
    }
    loop_thread.join();
}

TEST_P(EventLoopTest, MassConcurrencyPostWithHeavyWorkload) {
    if (mLoopType == "qemu") {
        GTEST_SKIP() << "QemuEventLoop stubs do not support this scenario.";
    }
    const int num_threads = 8;
    const int tasks_per_thread = 100;
    const int total_tasks = num_threads * tasks_per_thread;
    absl::BlockingCounter counter(total_tasks);

    std::atomic<int64_t> total_sum = 0;

    // Start the event loop in its own thread.
    std::thread loop_thread([&]() { (void)loop->run(); });

    constexpr int sum_up_to = 1000;
    // Create worker threads to post tasks with a small workload.
    std::vector<std::thread> worker_threads;
    for (int i = 0; i < num_threads; ++i) {
        worker_threads.emplace_back([&]() {
            for (int j = 0; j < tasks_per_thread; ++j) {
                (void)loop->post([&]() {
                    // Perform a small amount of work to simulate a real task.
                    int64_t local_sum = 0;
                    for (int k = 0; k <= sum_up_to; ++k) {
                        local_sum += k;
                    }
                    total_sum += local_sum;
                    counter.DecrementCount();
                });
            }
        });
    }

    counter.Wait();

    // The expected sum is the sum of numbers from 0 to 999, repeated
    // for each of the total tasks. sum(i, n) = n/2 + (n + 1)
    int64_t expected_local_sum = (sum_up_to / 2) * (sum_up_to + 1);
    int64_t expected_total_sum = expected_local_sum * total_tasks;

    EXPECT_EQ(total_sum.load(), expected_total_sum);

    loop->shutdown(1s).wait();
    loop->stop();
    for (auto& t : worker_threads) {
        t.join();
    }
    loop_thread.join();
}

TEST_P(EventLoopTest, MassConcurrencyPostWithDataIntegrity) {
    if (mLoopType == "qemu") {
        GTEST_SKIP() << "QemuEventLoop stubs do not support this scenario.";
    }
    const int num_threads = 4;
    const int tasks_per_thread = 500;
    const int total_tasks = num_threads * tasks_per_thread;
    absl::BlockingCounter counter(total_tasks);

    // A vector that is only ever modified from the event loop thread.
    std::vector<int> ordered_sequence;

    // Start the event loop in its own thread.
    std::thread loop_thread([&]() { (void)loop->run(); });

    // Create worker threads. Each thread posts a task with a unique ID.
    std::vector<std::thread> worker_threads;
    for (int i = 0; i < num_threads; ++i) {
        worker_threads.emplace_back([&, thread_id = i]() {
            for (int j = 0; j < tasks_per_thread; ++j) {
                (void)loop->post([&, val = (thread_id * tasks_per_thread) + j]() {
                    ordered_sequence.push_back(val);
                    counter.DecrementCount();
                });
            }
        });
    }

    // Wait for all tasks to be executed.
    counter.Wait();

    // The order of execution is not guaranteed, but the number of elements should be correct.
    ASSERT_EQ(ordered_sequence.size(), total_tasks);

    // Sort the vector to check for integrity.
    std::sort(ordered_sequence.begin(), ordered_sequence.end());

    // Create the expected sequence of numbers.
    std::vector<int> expected_sequence(total_tasks);
    std::iota(expected_sequence.begin(), expected_sequence.end(), 0);

    // Verify that all numbers are present and no duplicates or missing values exist.
    EXPECT_EQ(ordered_sequence, expected_sequence);

    loop->shutdown(1s).wait();
    loop->stop();
    for (auto& t : worker_threads) {
        t.join();
    }
    loop_thread.join();
}

// --- Tests for Fire-and-Forget `post` ---

TEST_P(EventLoopTest, PostDelayedExecutesAfterDelay) {
    runInThread();

    std::promise<void> task_completed;
    const auto delay = std::chrono::milliseconds(50);
    auto start_time = std::chrono::steady_clock::now();

    (void)loop->post(
            [&]() {
                if (mLoopType == "libuv") {
                    auto elapsed = std::chrono::steady_clock::now() - start_time;
                    EXPECT_NEAR(
                            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
                            delay.count(), tolerance.count());
                }
                task_completed.set_value();
            },
            delay);

    auto future = task_completed.get_future();
    runUntil(future);
}

TEST_P(EventLoopTest, ScheduleDelayedHelperExecutesSuccessfully) {
    runInThread();

    std::promise<void> task_completed;
    const auto delay = std::chrono::milliseconds(50);
    auto start_time = std::chrono::steady_clock::now();

    auto handle = loop->scheduleDelayed(
            [&]() {
                if (mLoopType == "libuv") {
                    auto elapsed = std::chrono::steady_clock::now() - start_time;
                    EXPECT_NEAR(
                            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
                            delay.count(), tolerance.count());
                }
                task_completed.set_value();
            }, delay);

    auto future = task_completed.get_future();
    runUntil(future);
    ASSERT_NE(handle, nullptr);
}

TEST_P(EventLoopTest, ScheduleDelayedExecutesSuccessfully) {
    runInThread();

    std::promise<void> task_completed;
    const auto delay = std::chrono::milliseconds(50);
    auto start_time = std::chrono::steady_clock::now();

    auto handle = loop->createTimer(
            [&]() {
                if (mLoopType == "libuv") {
                    auto elapsed = std::chrono::steady_clock::now() - start_time;
                    EXPECT_NEAR(
                            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
                            delay.count(), tolerance.count());
                }
                task_completed.set_value();
            });
    handle->schedule(delay);

    auto future = task_completed.get_future();

    runUntil(future);
    ASSERT_NE(handle, nullptr);
}

TEST_P(EventLoopTest, ScheduleDelayedIsReschedulableAfterFiring) {
    runInThread();

    std::atomic<bool> task_executed = false;
    auto handle = loop->createTimer([&]() { task_executed = true; });
    handle->schedule(100ms);

    // Advance time past the timer's expiration.
    if (mLoopType == "qemu") {
        fake_qemu_advance_ms(150);
    } else {
        std::this_thread::sleep_for(150ms);
    }

    ASSERT_TRUE(task_executed.load());
    task_executed = false;

    handle->schedule(100ms);

    // Advance time past the timer's expiration.
    if (mLoopType == "qemu") {
        fake_qemu_advance_ms(150);
    } else {
        std::this_thread::sleep_for(150ms);
    }
    ASSERT_TRUE(task_executed.load());
}

TEST_P(EventLoopTest, ScheduleDelayedIsCancelledByHandle) {
    runInThread();

    std::atomic<bool> task_executed = false;
    auto handle = loop->createTimer([&]() { task_executed = true; });
    handle->schedule(100ms, 0ms);

    handle->cancel();

    // Advance time past the timer's expiration.
    if (mLoopType == "qemu") {
        fake_qemu_advance_ms(150);
    } else {
        std::this_thread::sleep_for(150ms);
    }

    ASSERT_FALSE(task_executed.load());
}

TEST_P(EventLoopTest, ScheduleDelayedIsCancelledByRAII) {
    runInThread();
    std::atomic<bool> task_executed = false;

    {
        auto handle = loop->createTimer([&]() { task_executed = true; });
        handle->schedule(100ms, 0ms);
        VLOG(1) << "Use count: " << handle.use_count();
    }  // handle is destroyed here, cancelling the timer.

    if (mLoopType == "qemu") {
        fake_qemu_advance_ms(150);
    } else {
        std::this_thread::sleep_for(150ms);
    }

    ASSERT_FALSE(task_executed.load());
}

TEST_P(EventLoopTest, ScheduleRepeatingHelperExecutesMultipleTimes) {
    runInThread();
    std::promise<void> promise;
    std::atomic<int> counter = 0;
    const int target_count = 3;

    auto handle = loop->scheduleRepeating(
            [&]() {
                if (++counter == target_count) {
                    promise.set_value();
                }
            },
    10ms,   // Initial delay
    50ms);  // Interval

    auto future = promise.get_future();
    runUntil(future);
    handle->cancel();
    ASSERT_EQ(counter.load(), target_count);
}

TEST_P(EventLoopTest, ScheduleRepeatingExecutesMultipleTimes) {
    runInThread();
    std::promise<void> promise;
    std::atomic<int> counter = 0;
    const int target_count = 3;

    auto handle = loop->createTimer(
            [&]() {
                if (++counter == target_count) {
                    promise.set_value();
                }
            });
    handle->schedule(
    10ms,   // Initial delay
    50ms);  // Interval

    auto future = promise.get_future();
    runUntil(future);
    handle->cancel();
    ASSERT_EQ(counter.load(), target_count);
}

TEST_P(EventLoopTest, MultiThreadedCreationAndCancellation) {
    if (mLoopType == "qemu") {
        GTEST_SKIP() << "QemuEventLoop stubs do not support this scenario.";
    }
    // Use a shared_ptr to ensure the EventLoop is not destroyed
    // while the worker threads are still using it.
    runInThread();
    // The shared queue for timer handles.
    std::queue<std::shared_ptr<EventLoop::Timer>> timer_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;

    const int num_tasks_per_creator = 10;
    const int num_creators = 100;
    const int total_tasks_to_process = num_creators * num_tasks_per_creator;

    // Atomic counters for verification
    std::atomic<int> created_task_count = 0;
    std::atomic<int> canceled_task_count = 0;

    // A blocking counter to wait for all creator threads to finish.
    std::atomic_int creators_done = num_creators;
    // A blocking counter to wait for all cancellations to occur.
    std::atomic_int cancellations_done = total_tasks_to_process;

    // Thread function for creating tasks.
    auto creator_thread_func = [&, loop_ptr = loop](int creator_id) {
        for (int i = 0; i < num_tasks_per_creator; ++i) {
            // Schedule a repeating timer.
            auto handle = loop_ptr->createTimer(
                    [&]() { /* Task body not critical for this test */ });
            handle->schedule(
                    std::chrono::milliseconds(10),  // Initial delay
                    std::chrono::seconds(10)        // Long interval to avoid accidental ticks
            );

            // Push the handle into the shared queue.
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                timer_queue.push(handle);
                created_task_count++;
            }
            queue_cv.notify_one();
        }
        --creators_done;
    };

    // Thread function for canceling tasks.
    auto canceller_thread_func = [&]() {
        // Continue while there are still tasks to cancel.
        while (cancellations_done > 0) {
            std::shared_ptr<EventLoop::Timer> handle;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                // Wait for a task or for all creators to finish.
                queue_cv.wait(lock, [&] {
                    return !timer_queue.empty() || (creators_done == 0 && timer_queue.empty());
                });

                if (timer_queue.empty() && cancellations_done > 0) {
                    // This could happen if creators are still working but the queue is temporarily
                    // empty. A final wait and exit check is needed to ensure we don't prematurely
                    // exit.
                    continue;
                }

                if (timer_queue.empty() && creators_done == 0) {
                    break;
                }

                handle = timer_queue.front();

                timer_queue.pop();
            }

            if (handle) {
                handle->cancel();
                canceled_task_count++;
                cancellations_done--;
            }
        }
    };

    // Start creator threads.
    std::vector<std::thread> creator_threads;
    for (int i = 0; i < num_creators; ++i) {
        creator_threads.emplace_back(creator_thread_func, i);
    }

    // Start the canceller thread.
    LOG(INFO) << "Starting cancel";
    std::thread canceller_thread(canceller_thread_func);

    // Wait for all creator threads to finish.
    for (auto& t : creator_threads) {
        t.join();
    }

    // Notify the canceller one last time in case it's waiting on an empty queue.
    queue_cv.notify_one();

    // Wait for the canceller thread to finish.
    canceller_thread.join();

    // Assertions to verify the test's success.
    ASSERT_EQ(created_task_count, total_tasks_to_process);
    ASSERT_EQ(canceled_task_count, total_tasks_to_process);
}

TEST_P(EventLoopTest, ScheduleRepeatingIsCancelledMidway) {
    runInThread();
    std::atomic<int> counter = 0;

    auto handle = loop->createTimer([&]() { counter++; });
    handle->schedule(10ms, 40ms);

    if (mLoopType == "qemu") {
        fake_qemu_advance_ms(100);
    } else {
        std::this_thread::sleep_for(100ms);
    }

    handle->cancel();
    int count_after_cancel = counter.load();

    if (mLoopType == "qemu") {
        fake_qemu_advance_ms(100);
    } else {
        std::this_thread::sleep_for(100ms);
    }

    EXPECT_EQ(counter.load(), count_after_cancel);
    EXPECT_GE(count_after_cancel, 2);
    EXPECT_LE(count_after_cancel, 5)
            << "Even under heavy load, we shouldn't get more than 2 duplicate invocations.";
}

TEST_P(EventLoopTest, ThreadedEventLoopWaitsAtMostTimeout) {
    if (mLoopType == "qemu") {
        GTEST_SKIP() << "ThreadedEventLoop is not compatible with the singleton QemuEventLoop.";
    }
    std::promise<void> task_completed;
    auto start_time = std::chrono::steady_clock::now();
    std::shared_ptr<EventLoop::Timer> task;
    {
        loop = nullptr;
        auto tloop = ThreadedEventLoop::create(std::move(mLibuvLoop));
        (void)tloop->post([&]() { task_completed.set_value(); }, std::chrono::seconds(10));
        start_time = std::chrono::steady_clock::now();
    }
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
              ThreadedEventLoop::getTimeout().count() + tolerance.count());
}

TEST_P(EventLoopTest, ShutdownRaceConditionStressTest) {
    if (mLoopType == "qemu") {
        GTEST_SKIP() << "QemuEventLoop stubs do not support this scenario.";
    }
    // --- Test Setup ---
    const int kCreatorThreads = 4;
    const int kTimersPerThread = 250;
    const int kTotalTimers = kCreatorThreads * kTimersPerThread;

    // A shared vector to hold all timer handles, protected by a mutex.
    absl::Mutex vec_mutex;
    std::vector<std::shared_ptr<EventLoop::Timer>> all_timers;
    all_timers.reserve(kTotalTimers);

    // Start the event loop in the background.
    runInThread();

    // --- Phase 1: Create many timers from multiple threads ---
    std::vector<std::thread> creators;
    for (int i = 0; i < kCreatorThreads; ++i) {
        creators.emplace_back([&]() {
            for (int j = 0; j < kTimersPerThread; ++j) {
                // Create long-running timers so they don't fire during the test.
                auto handle = loop->createTimer([]() { /* no-op */ });
                handle->schedule(1h, 1h);

                absl::MutexLock lock(&vec_mutex);
                all_timers.push_back(handle);
            }
        });
    }
    for (auto& t : creators) {
        t.join();
    }
    ASSERT_EQ(all_timers.size(), kTotalTimers);

    // --- Phase 2: Engineer the Race Condition ---

    // 1. Post a "slow" task to the event loop. This creates a delay,
    //    giving other threads a window to act before the shutdown task runs.
    (void)loop->post([]() { std::this_thread::sleep_for(std::chrono::milliseconds(50)); });

    // 2. Call shutdown IMMEDIATELY. Its logic is now queued *behind* the slow task.
    //    We will let TearDown() call the actual shutdown, but we need to start
    //    the destroyers now.
    absl::Notification shutdown_future_available;
    std::future<absl::Status> shutdown_future;
    auto shutdown = std::thread([lp = loop, &shutdown_future_available, &shutdown_future] {
        shutdown_future = lp->shutdown(std::chrono::milliseconds(500));
        shutdown_future_available.Notify();
    });

    // 3. Start destroyer threads. These threads will race against the shutdown
    //    task. They will destroy timers while the shutdown task is waiting in the queue.
    std::vector<std::thread> destroyers;
    const int kDestroyerThreads = 4;
    for (int i = 0; i < kDestroyerThreads; ++i) {
        destroyers.emplace_back([&]() {
            while (true) {
                std::shared_ptr<EventLoop::Timer> handle;
                {
                    absl::MutexLock lock(&vec_mutex);
                    if (all_timers.empty()) {
                        break;  // No more timers to destroy
                    }
                    handle = all_timers.back();
                    all_timers.pop_back();
                }
                // Resetting the shared_ptr triggers the timer's destructor on THIS thread.
                handle.reset();
            }
        });
    }

    // Wait for all destroyers to finish.
    for (auto& t : destroyers) {
        t.join();
    }
    shutdown_future_available.WaitForNotification();
    auto shutdown_status = shutdown_future.get();
    EXPECT_TRUE(shutdown_status.ok()) << "Shutdown failure: " << shutdown_status;
    shutdown.join();

    // The test's main assertion is that it doesn't crash.
    // The real result comes from the ASan/TSan output.
    // The fixture's TearDown will now perform the shutdown and cleanup.
}

TEST_P(EventLoopTest, CancelTimerFromTaskCallback) {
    runInThread();

    std::promise<void> task_completed_promise;
    auto future = task_completed_promise.get_future();
    std::promise<bool> timer_callback_promise;
    auto timer_executed = timer_callback_promise.get_future();

    // Timer callback, declared here so it will not go out of scope
    // and get cancelled.
    std::shared_ptr<EventLoop::Timer> handle;

    // Post a task to the event loop.

    (void)loop->post([&]() {
        // Inside the task, create a timer.
        std::weak_ptr<EventLoop::Timer> weak_handle;

        // Create the timer. The `shared_ptr` (`handle`) will keep it alive
        // for this scope. The EventLoop also holds a reference.
        handle = loop->createTimer(
                // Note we capture a pointer to handle, as we are just initializing it!
                [h = &handle, &timer_callback_promise]() {
                    (*h)->cancel();
                    timer_callback_promise.set_value(true);
                });
        handle->schedule(10ms, 0ms);

        // After the `handle` is created, point the `weak_handle` to it.
        // The lambda now holds a reference to this `weak_handle`.
        weak_handle = handle;

        // Signal that the inner task has been cancelled
        task_completed_promise.set_value();
    });

    // Wait for the posted task to finish.
    runUntil(future);

    // Now, wait a bit longer to ensure the timer *would* have fired if not
    // cancelled.
    if (mLoopType == "qemu") {
        fake_qemu_advance_ms(20);
    } else {
        std::this_thread::sleep_for(20ms);
    }

    runUntil(timer_executed);
    // The main assertion: the timer's callback should have run,
    // no deadlocks.
    ASSERT_TRUE(timer_executed.get());
}

TEST_P(EventLoopTest, NoTsanFailuresOnLaunch) {
    if (mLoopType == "qemu") {
        GTEST_SKIP() << "This test is specific to the LibuvEventLoop lifecycle.";
    }

    // This test validates a race-free shutdown of the event loop immediately after its creation.
    //
    // The problem this test addresses is a potential race condition between a newly created
    // ThreadedEventLoop and its shutdown sequence. In the old implementation, the event loop's
    // state might have been prematurely marked as "RUNNING" or its shutdown process initiated
    // before the worker thread had fully entered the `uv_run` loop. This could lead to:
    //
    // 1. **Data Race:** A race between the main thread calling `uv_stop` during shutdown and the
    //    worker thread reading the loop's state inside `uv_run`. This is a classic read/write
    //    race on shared state, which ThreadSanitizer (TSan) would correctly flag.
    //
    // 2. **Premature Shutdown:** The main thread's `shutdown` and `stop` calls could execute
    //    before the `uv_run` loop was fully initialized, causing the teardown to operate on
    //    an incomplete or invalid state.
    //
    // The current implementation in `ThreadedEventLoop` correctly addresses this by posting
    // a task to the event loop that updates the state to "RUNNING" and by ensuring the constructor
    // will finish after the event loop is marked as running.
    //
    // The test confirms this by creating a `ThreadedEventLoop` and immediately shutting it
    // down, verifying that no TSan failures or crashes occur.
    loop = nullptr;
    auto threaded_loop = ThreadedEventLoop::create(std::move(mLibuvLoop));
    auto future = threaded_loop->shutdown(1s);
    future.wait_for(1s);
    ASSERT_TRUE(future.get().ok()) << "Shutdown failed, the thread host run is likely not active.";
    threaded_loop->stop();
}

TEST_P(EventLoopTest, RescheduleRepeatingTimer) {
    runInThread();
    std::atomic<int> counter = 0;
    std::promise<void> fired1_promise, fired2_promise, fired3_promise;
    auto fired1_future = fired1_promise.get_future();
    auto fired2_future = fired2_promise.get_future();
    auto fired3_future = fired3_promise.get_future();

    // Shared pointers to hold timestamps to be checked inside the callback
    auto schedule_time = std::make_shared<std::chrono::steady_clock::time_point>();
    auto reschedule_time = std::make_shared<std::chrono::steady_clock::time_point>();
    auto last_fire_time = std::make_shared<std::chrono::steady_clock::time_point>();

    auto handle = loop->createTimer(
            [&, schedule_time, reschedule_time, last_fire_time]() {
                auto now = std::chrono::steady_clock::now();
                int c = ++counter;

                if (mLoopType == "libuv") {
                    if (c == 1) {
                        auto elapsed = now - *schedule_time;
                        EXPECT_NEAR(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
                                            .count(),
                                    100, tolerance.count());
                    } else if (c == 2) {
                        auto elapsed = now - *reschedule_time;
                        EXPECT_NEAR(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
                                            .count(),
                                    200, tolerance.count());
                    } else if (c == 3) {
                        auto elapsed = now - *last_fire_time;
                        EXPECT_NEAR(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
                                            .count(),
                                    200, tolerance.count());
                    }
                }
                *last_fire_time = now;

                if (c == 1) fired1_promise.set_value();
                if (c == 2) fired2_promise.set_value();
                if (c == 3) fired3_promise.set_value();
            });
    handle->schedule(100ms, 100ms);

    *schedule_time = std::chrono::steady_clock::now();

    // Let it fire once.
    runUntil(fired1_future);
    ASSERT_EQ(counter.load(), 1);

    // Reschedule to fire sooner and more frequently.
    *reschedule_time = std::chrono::steady_clock::now();
    handle->schedule(200ms, 200ms);

    // Check that it fires again quickly.
    runUntil(fired2_future);
    ASSERT_EQ(counter.load(), 2);

    // Check that it fires again quickly.
    runUntil(fired3_future);
    ASSERT_GE(counter.load(), 3);
}

TEST_P(EventLoopTest, TimerDestroyedAfterLoop) {
    runInThread();

    auto timer = loop->createTimer([]() {});

    shutdown();

    timer.reset();
}

TEST_P(EventLoopTest, TimerScheduleAfterLoopDestroyed) {
    runInThread();

    auto timer = loop->createTimer([]() {});

    shutdown();

    timer->schedule(100ms, 100ms);

    timer.reset();
}

TEST_P(EventLoopTest, TimerCreatedAfterLoopShutdown) {
    runInThread();

    auto f = loop->shutdown(5s);
    if (mLoopType == "qemu") {
        // Need to run Qemu "thread" for shutdown to complete
        fake_qemu_advance_ms(150);
    }
    f.wait();
    loop->stop();

    auto timer = loop->createTimer([]() {});

    timer->schedule(100ms, 100ms);

    timer.reset();

    loop = nullptr;
}

TEST_P(EventLoopTest, TimerCreatedDuringLoopShutdown) {
    runInThread();

    auto f = loop->shutdown(5s);
    auto timer = loop->createTimer([]() {});
    if (mLoopType == "qemu") {
        // Need to run Qemu "thread" for shutdown to complete
        fake_qemu_advance_ms(150);
    }
    f.wait();
    loop->stop();

    timer->schedule(100ms, 100ms);

    timer.reset();

    loop = nullptr;
}
TEST_P(EventLoopTest, LibuvEventStateChanges) {
    if (mLoopType == "qemu") {
        GTEST_SKIP() << "This test is specific to the LibuvEventLoop lifecycle.";
    }

    std::vector<LooperStatusEvent::State> states;
    absl::Notification finished;
    absl::Notification running;

    auto subscription =
            android::base::eventing::makeScopedCallback(*loop, [&](const LooperStatusEvent& event) {
                states.push_back(event.state);
                LOG(ERROR) << event;
                if (event.state == LooperStatusEvent::State::RUNNING) {
                    running.Notify();
                }
                if (event.state == LooperStatusEvent::State::FINISHED) {
                    finished.Notify();
                }
            });

    runInThread();
    running.WaitForNotification();
    auto status = loop->shutdown(1s).get();
    ASSERT_TRUE(status.ok());
    loop->stop();
    finished.WaitForNotification();

    ASSERT_THAT(states, ::testing::ElementsAre(LooperStatusEvent::State::RUNNING,
                                               LooperStatusEvent::State::SHUTTING_DOWN,
                                               LooperStatusEvent::State::FINISHED));
}

TEST_P(EventLoopTest, ThreadedEventStateChanges) {
    if (mLoopType == "qemu") {
        GTEST_SKIP() << "ThreadedEventLoop is not compatible with the singleton QemuEventLoop.";
    }

    loop = nullptr;
    auto threaded_loop = ThreadedEventLoop::create(std::move(mLibuvLoop));
    std::vector<LooperStatusEvent::State> states;
    absl::Notification finished;

    auto subscription = android::base::eventing::makeScopedCallback(
            *threaded_loop, [&](const LooperStatusEvent& event) {
                states.push_back(event.state);
                LOG(ERROR) << "state: " << event;
                if (event.state == LooperStatusEvent::State::FINISHED) {
                    finished.Notify();
                }
            });

    auto status = threaded_loop->shutdown(1s).get();
    ASSERT_TRUE(status.ok());
    threaded_loop->stop();
    finished.WaitForNotification();

    ASSERT_THAT(states, ::testing::ElementsAre(LooperStatusEvent::State::SHUTTING_DOWN,
                                               LooperStatusEvent::State::FINISHED));
}

}  // namespace goldfish::async
