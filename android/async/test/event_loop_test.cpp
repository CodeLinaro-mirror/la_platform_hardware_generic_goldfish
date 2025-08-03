#include "goldfish/async/event_loop.h"

#include <gtest/gtest.h>

#include <future>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

#include "absl/log/log.h"
#include "absl/synchronization/blocking_counter.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"

using namespace std::chrono_literals;

namespace goldfish::async {
// Test fixture to provide a fresh EventLoop for each test.
class EventLoopTest : public ::testing::Test {
  protected:
    void SetUp() override { loop = std::make_unique<LibuvEventLoop>(); }

    std::unique_ptr<EventLoop> loop;
};

// =================================================================
//                      TEST CASES
// =================================================================

TEST_F(EventLoopTest, ScheduleAndExecuteSingleTask) {
    std::promise<bool> task_executed_promise;
    auto future = task_executed_promise.get_future();

    // Schedule a simple task that fulfills the promise.
    loop->post([&]() { task_executed_promise.set_value(true); });

    // Run the loop in a separate thread.
    std::thread loop_thread([&]() { loop->run(); });

    // The main thread blocks here, waiting for the task to run.
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
    EXPECT_TRUE(future.get());

    loop->stop();
    loop_thread.join();
}

TEST_F(EventLoopTest, ScheduleAndExecuteSingleTaskOnRunningLoop) {
    std::promise<bool> task_executed_promise;
    auto future = task_executed_promise.get_future();
    ThreadedEventLoop running_loop(std::move(loop));

    // Schedule a simple task that fulfills the promise.
    running_loop.post([&]() { task_executed_promise.set_value(true); });

    // The main thread blocks here, waiting for the task to run.
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
    EXPECT_TRUE(future.get());
}

TEST_F(EventLoopTest, TasksExecuteInScheduledOrder) {
    std::promise<std::vector<int>> results_promise;
    auto future = results_promise.get_future();

    std::vector<int> results;
    std::mutex results_mutex;
    const int task_count = 100;

    // Schedule a series of tasks that push numbers into the vector.
    for (int i = 0; i < task_count; ++i) {
        loop->post([&, i]() {
            std::lock_guard<std::mutex> lock(results_mutex);
            results.push_back(i);
        });
    }
    // Schedule a final task to fulfill the promise with the collected results.
    loop->post([&]() { results_promise.set_value(results); });

    std::thread loop_thread([&]() { loop->run(); });

    // Verify that the final task ran and returned the results.
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);

    std::vector<int> final_results = future.get();

    // Create the expected sequence of numbers.
    std::vector<int> expected_results(task_count);
    std::iota(expected_results.begin(), expected_results.end(), 0);

    // Check if the executed order matches the postd order.
    EXPECT_EQ(final_results, expected_results);

    loop->stop();
    loop_thread.join();
}

TEST_F(EventLoopTest, IsOnLoopThreadIsCorrect) {
    std::promise<bool> check_promise;
    auto future = check_promise.get_future();

    // At the start, from the main thread, it should be false.
    EXPECT_FALSE(loop->isOnLoopThread());

    // Schedule a task to check from within the loop's thread.
    loop->post([&]() { check_promise.set_value(loop->isOnLoopThread()); });

    std::thread loop_thread([&]() {
        // Checking from the loop thread before run() is still false.
        EXPECT_FALSE(loop->isOnLoopThread());
        loop->run();
    });

    // Wait for the result from the postd task.
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
    EXPECT_TRUE(future.get());

    loop->stop();
    loop_thread.join();
}

TEST_F(EventLoopTest, RunExitsWhenStopped) {
    std::atomic<bool> is_running = false;

    std::thread loop_thread([&]() {
        is_running = true;
        loop->run();
        is_running = false;
    });

    // Give the thread a moment to start and enter run().
    while (!is_running) {
        std::this_thread::sleep_for(10ms);
    }

    // Now, stop the loop from the main thread.
    loop->stop();

    // The thread should exit the run() method and join cleanly.
    // The test passes if join() doesn't hang.
    loop_thread.join();

    EXPECT_FALSE(is_running);
}

TEST_F(EventLoopTest, CanScheduleTaskFromAnotherThread) {
    std::promise<bool> task_ran_promise;
    auto future = task_ran_promise.get_future();

    // Start the event loop in its own thread.
    std::thread loop_thread([&]() { loop->run(); });

    // From a *different* worker thread, post a task.
    std::thread worker_thread(
            [&]() { (void)loop->post([&]() { task_ran_promise.set_value(true); }); });

    // Wait for the result.
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
    EXPECT_TRUE(future.get());

    loop->stop();
    worker_thread.join();
    loop_thread.join();
}

TEST_F(EventLoopTest, CanScheduleTaskFromWithinAnotherTask) {
    std::promise<int> final_task_promise;
    auto future = final_task_promise.get_future();

    // Schedule a task that, in turn, posts another task.
    (void)loop->post([&]() {
        // This is Task 1.
        loop->post([&]() {
            // This is Task 2.
            final_task_promise.set_value(42);
        });
    });

    std::thread loop_thread([&]() { loop->run(); });

    // Wait for the nested task (Task 2) to complete.
    ASSERT_EQ(future.wait_for(1s), std::future_status::ready);
    EXPECT_EQ(future.get(), 42);

    loop->stop();
    loop_thread.join();
}

TEST_F(EventLoopTest, MassConcurrencyPost) {
    const int num_threads = 8;
    const int tasks_per_thread = 1000;
    const int total_tasks = num_threads * tasks_per_thread;
    absl::BlockingCounter counter(total_tasks);

    // A shared counter to track completed tasks on the event loop thread.
    std::atomic<int> completed_tasks_count = 0;

    // Start the event loop in its own thread.
    std::thread loop_thread([&]() { loop->run(); });

    // Create multiple threads, each posting tasks.
    std::vector<std::thread> worker_threads;
    for (int i = 0; i < num_threads; ++i) {
        worker_threads.emplace_back([&]() {
            for (int j = 0; j < tasks_per_thread; ++j) {
                loop->post([&]() {
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
    loop->stop();
    for (auto& t : worker_threads) {
        t.join();
    }
    loop_thread.join();
}

TEST_F(EventLoopTest, MassConcurrencyPostWithHeavyWorkload) {
    const int num_threads = 8;
    const int tasks_per_thread = 100;
    const int total_tasks = num_threads * tasks_per_thread;
    absl::BlockingCounter counter(total_tasks);

    std::atomic<int64_t> total_sum = 0;

    // Start the event loop in its own thread.
    std::thread loop_thread([&]() { loop->run(); });

    constexpr int sum_up_to = 1000;
    // Create worker threads to post tasks with a small workload.
    std::vector<std::thread> worker_threads;
    for (int i = 0; i < num_threads; ++i) {
        worker_threads.emplace_back([&]() {
            for (int j = 0; j < tasks_per_thread; ++j) {
                loop->post([&]() {
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

    loop->stop();
    for (auto& t : worker_threads) {
        t.join();
    }
    loop_thread.join();
}

TEST_F(EventLoopTest, MassConcurrencyPostWithDataIntegrity) {
    const int num_threads = 4;
    const int tasks_per_thread = 500;
    const int total_tasks = num_threads * tasks_per_thread;
    absl::BlockingCounter counter(total_tasks);

    // A vector that is only ever modified from the event loop thread.
    std::vector<int> ordered_sequence;

    // Start the event loop in its own thread.
    std::thread loop_thread([&]() { loop->run(); });

    // Create worker threads. Each thread posts a task with a unique ID.
    std::vector<std::thread> worker_threads;
    for (int i = 0; i < num_threads; ++i) {
        worker_threads.emplace_back([&, thread_id = i]() {
            for (int j = 0; j < tasks_per_thread; ++j) {
                loop->post([&, val = (thread_id * tasks_per_thread) + j]() {
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

    loop->stop();
    for (auto& t : worker_threads) {
        t.join();
    }
    loop_thread.join();
}

}  // namespace goldfish::async