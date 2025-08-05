#include "goldfish/async/event_loop.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"

using namespace std::chrono_literals;

namespace goldfish::async {
// Test fixture to provide a fresh EventLoop for each test.
class EventLoopTest : public ::testing::Test {
  protected:
    void SetUp() override { loop = std::make_unique<LibuvEventLoop>(); }

    void TearDown() override {
        if (loop_thread.joinable()) {
            // Initiate graceful shutdown and give it a chance to cleanup
            loop->shutdown(1s).wait();
            loop->stop();
            loop_thread.join();
        }
        loop.reset();
    }
    void runInThread() {
        loop_thread = std::thread([this]() { loop->run(); });
    }

    // Note, we have high tolerance due to our build bots being under quite some load.
    const std::chrono::milliseconds tolerance = std::chrono::milliseconds(25);
    std::unique_ptr<EventLoop> loop;
    std::thread loop_thread;
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
    loop->shutdown(1s).wait();
    loop->stop();
    loop_thread.join();
}

TEST_F(EventLoopTest, PostAndWaitFromLoopThreadFails) {
    std::promise<void> status_promise;
    auto status_future = status_promise.get_future();

    runInThread();

    // Post a task that will execute on the event loop's thread.
    loop->post([this, &status_promise]() {
        // Now, from within the loop's thread, call postAndWait.
        // This is expected to fail immediately to prevent a deadlock.
        EXPECT_DEATH(absl::StatusOr<bool> status_or = this->loop->postAndWait([]() {
            // This inner task should never be executed.
            // If it does run, the test should fail immediately.
            return false;
        }),
                     "postAndWait cannot be called from the event loop.");

        // The call should have returned a status object, not a value.
        // Fulfill the promise with the status we received.
        status_promise.set_value();
    });

    // Wait for the status to be reported back from the loop thread.
    status_future.get();
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

    loop->shutdown(1s).wait();
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

    loop->shutdown(1s).wait();
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
    loop->shutdown(1s).wait();
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

    loop->shutdown(1s).wait();
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

    loop->shutdown(1s).wait();
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
    loop->shutdown(1s).wait();
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

    loop->shutdown(1s).wait();
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

    loop->shutdown(1s).wait();
    loop->stop();
    for (auto& t : worker_threads) {
        t.join();
    }
    loop_thread.join();
}

// --- Tests for Fire-and-Forget `post` ---

TEST_F(EventLoopTest, PostDelayedExecutesAfterDelay) {
    runInThread();

    std::promise<void> task_completed;
    std::atomic<bool> executed = false;
    const auto delay = std::chrono::milliseconds(50);
    auto start_time = std::chrono::steady_clock::now();

    // Post the delayed task.
    loop->post(
            [&]() {
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                EXPECT_NEAR(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
                            delay.count(), tolerance.count());
                executed = true;
                task_completed.set_value();
            },
            delay);

    // Wait for the task to finish.
    task_completed.get_future().wait();
    ASSERT_TRUE(executed.load());
}

// --- Tests for Cancellable `scheduleDelayed` ---

TEST_F(EventLoopTest, ScheduleDelayedExecutesSuccessfully) {
    runInThread();

    std::promise<void> task_completed;
    std::atomic<bool> executed = false;
    const auto delay = std::chrono::milliseconds(50);
    auto start_time = std::chrono::steady_clock::now();

    // Schedule the task and keep the handle.
    auto handle = loop->scheduleDelayed(
            [&]() {
                auto elapsed = std::chrono::steady_clock::now() - start_time;
                EXPECT_NEAR(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
                            delay.count(), tolerance.count());
                executed = true;
                task_completed.set_value();
            },
            delay);

    // Wait for the task to finish and verify it ran.
    task_completed.get_future().wait();
    ASSERT_TRUE(executed.load());
    ASSERT_NE(handle, nullptr);
}

TEST_F(EventLoopTest, ScheduleDelayedIsCancelledByHandle) {
    runInThread();

    std::promise<void> verification_complete;
    std::atomic<bool> task_executed = false;

    // Schedule a task with a moderate delay.
    auto handle =
            loop->scheduleDelayed([&]() { task_executed = true; }, std::chrono::milliseconds(100));

    // Immediately cancel it.
    handle->cancel();

    // Post a verifier task to run after the original task should have.
    loop->post([&]() { verification_complete.set_value(); }, std::chrono::milliseconds(150));

    // Wait for verification and ensure the original task never ran.
    verification_complete.get_future().wait();
    ASSERT_FALSE(task_executed.load());
}

TEST_F(EventLoopTest, ScheduleDelayedIsCancelledByRAII) {
    runInThread();

    std::promise<void> verification_complete;
    std::atomic<bool> task_executed = false;

    {
        // Schedule a task, but let its handle go out of scope immediately.
        std::shared_ptr<EventLoop::Timer> handle = loop->scheduleDelayed(
                [&]() {
                    LOG(WARNING) << "Running cancelled task";
                    task_executed = true;
                },
                std::chrono::milliseconds(100));
        // handle is destroyed here, cancelling the timer.
    }
    std::this_thread::sleep_for(50ms);
    // Post a verifier task to check the state later.
    loop->post([&]() { verification_complete.set_value(); }, std::chrono::milliseconds(150));

    // Wait and verify the task was cancelled by the handle's destructor.
    verification_complete.get_future().wait();
    ASSERT_FALSE(task_executed.load());
}

// --- Tests for Cancellable `scheduleRepeating` ---

TEST_F(EventLoopTest, ScheduleRepeatingExecutesMultipleTimes) {
    runInThread();
    std::promise<void> promise;

    std::atomic<int> counter = 0;
    const int target_count = 3;

    // Schedule a task to repeat 3 times, then stop itself.
    auto handle = loop->scheduleRepeating(
            [&]() {
                if (++counter == target_count) {
                    promise.set_value();
                }
            },
            std::chrono::milliseconds(10),   // Initial delay
            std::chrono::milliseconds(50));  // Interval

    // Wait until the task has run 3 times.
    promise.get_future().wait();
    handle->cancel();  // Clean up the timer.
    ASSERT_EQ(counter.load(), target_count);
}

TEST_F(EventLoopTest, MultiThreadedCreationAndCancellation) {
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
    auto creator_thread_func = [&, loop_ptr = loop.get()](int creator_id) {
        for (int i = 0; i < num_tasks_per_creator; ++i) {
            // Schedule a repeating timer.
            auto handle = loop_ptr->scheduleRepeating(
                    [&]() { /* Task body not critical for this test */ },
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

TEST_F(EventLoopTest, ScheduleRepeatingIsCancelledMidway) {
    runInThread();

    std::promise<int> final_count_promise;
    std::atomic<int> counter = 0;

    // Schedule a rapidly repeating task.
    auto handle = loop->scheduleRepeating([&]() { counter++; }, std::chrono::milliseconds(10),
                                          std::chrono::milliseconds(40));

    // Let it run for a bit (should allow for ~2 executions).
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Cancel the timer.
    handle->cancel();

    // Sleep again to ensure no more tasks fire after cancellation.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Post one final task to get the final counter value from the loop thread.
    loop->post([&]() { final_count_promise.set_value(counter.load()); });

    int final_count = final_count_promise.get_future().get();

    // It should have run, but not too many times.
    EXPECT_GE(final_count, 2);
    EXPECT_LE(final_count, 3);
}

TEST_F(EventLoopTest, ThreadedEventLoopWaitsAtMostTimeout) {
    std::promise<void> task_completed;
    auto start_time = std::chrono::steady_clock::now();
    std::shared_ptr<EventLoop::Timer> task;
    {
        ThreadedEventLoop tloop(std::move(loop));
        tloop.post([&]() { task_completed.set_value(); }, std::chrono::seconds(10));
        start_time = std::chrono::steady_clock::now();
    }
    auto elapsed = std::chrono::steady_clock::now() - start_time;
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
              ThreadedEventLoop::getTimeout().count() + tolerance.count());
}

TEST_F(EventLoopTest, ShutdownRaceConditionStressTest) {
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
                auto handle = loop->scheduleRepeating([]() { /* no-op */ }, 1h, 1h);

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
    loop->post([]() { std::this_thread::sleep_for(std::chrono::milliseconds(50)); });

    // 2. Call shutdown IMMEDIATELY. Its logic is now queued *behind* the slow task.
    //    We will let TearDown() call the actual shutdown, but we need to start
    //    the destroyers now.
    absl::Notification shutdown_future_available;
    std::future<absl::Status> shutdown_future;
    auto shutdown = std::thread([lp = loop.get(), &shutdown_future_available, &shutdown_future] {
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

}  // namespace goldfish::async