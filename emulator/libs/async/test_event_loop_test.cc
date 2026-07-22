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

#include "goldfish/async/testing/test_event_loop.h"

#include <chrono>
#include <future>

#include "absl/status/status_matchers.h"
#include "gtest/gtest.h"

using namespace goldfish::async;
using namespace goldfish::async::testing;
using namespace std::chrono_literals;

TEST(TestEventLoop, PostSingleTask) {
    auto loop = TestEventLoop::Create();
    bool executed = false;
    loop->Post([&]() { executed = true; }).IgnoreError();
    ASSERT_FALSE(executed);
    loop->RunAll();
    ASSERT_TRUE(executed);
}

TEST(TestEventLoop, PostMultipleTasks) {
    auto loop = TestEventLoop::Create();
    int count = 0;
    loop->Post([&]() { count++; }).IgnoreError();
    loop->Post([&]() { count++; }).IgnoreError();
    loop->Post([&]() { count++; }).IgnoreError();
    ASSERT_EQ(0, count);
    loop->RunAll();
    ASSERT_EQ(3, count);
}

TEST(TestEventLoop, ScheduleDelayedTask) {
    auto loop = TestEventLoop::Create();
    bool executed = false;

    // Store the returned handle in a variable to keep it alive.
    auto timer = loop->ScheduleRepeating(
            [&]() {
                executed = true;
                return false;
            },
            100ms, 0ms);

    // only advance clock does something with timed task
    loop->RunAll();
    ASSERT_FALSE(executed);
    loop->AdvanceClock(99ms);
    ASSERT_FALSE(executed);
    loop->AdvanceClock(1ms);
    ASSERT_TRUE(executed);
}

TEST(TestEventLoop, ScheduleRepeatingTask) {
    auto loop = TestEventLoop::Create();
    int count = 0;
    auto timer = loop->ScheduleRepeating(
            [&]() {
                count++;
                return true;
            },
            100ms, 50ms);

    loop->AdvanceClock(100ms);
    ASSERT_EQ(1, count);

    loop->AdvanceClock(50ms);
    ASSERT_EQ(2, count);

    loop->AdvanceClock(50ms);
    ASSERT_EQ(3, count);

    // no new events after cancel.
    timer->Cancel();
    loop->AdvanceClock(50ms);
    ASSERT_EQ(3, count);
}

TEST(TestEventLoop, StopFromCallback) {
    auto loop = TestEventLoop::Create();
    int count = 0;

    auto timer = loop->ScheduleRepeating(
            [&]() {
                ++count;
                return count < 3;
            },
            100ms, 50ms);

    loop->AdvanceClock(100ms);
    ASSERT_EQ(1, count);

    loop->AdvanceClock(50ms);
    ASSERT_EQ(2, count);

    loop->AdvanceClock(50ms);
    ASSERT_EQ(3, count);

    loop->AdvanceClock(50ms);
    EXPECT_EQ(3, count);
    loop->AdvanceClock(50ms);
    EXPECT_EQ(3, count);
    loop->AdvanceClock(50ms);
    EXPECT_EQ(3, count);
}

TEST(TestEventLoop, PostAndWait) {
    auto loop = TestEventLoop::Create();
    auto future = std::async(std::launch::async,
                             [&]() { return loop->PostAndWait([]() { return 42; }); });

    while (future.wait_for(100ms) == std::future_status::timeout) {
        loop->RunAll();
    }
    auto s = future.get();
    ASSERT_THAT(s, absl_testing::IsOk());
    EXPECT_EQ(*s, 42);
}

TEST(TestEventLoop, PostAndWaitVoid) {
    auto loop = TestEventLoop::Create();
    bool executed = false;
    auto future = std::async(std::launch::async, [&]() {
        loop->PostAndWait([&]() { executed = true; }).IgnoreError();
    });
    while (future.wait_for(100ms) == std::future_status::timeout) {
        loop->RunAll();
    }
    future.get();
    ASSERT_TRUE(executed);
}

TEST(TestEventLoop, TimerCancellation) {
    auto loop = TestEventLoop::Create();
    bool executed = false;
    auto timer = loop->ScheduleRepeating(
            [&]() {
                executed = true;
                return false;
            },
            100ms, 0ms);

    timer->Cancel();
    loop->AdvanceClock(100ms);
    ASSERT_FALSE(executed);
}

TEST(TestEventLoop, TimerHandleDestructionCancels) {
    auto loop = TestEventLoop::Create();
    bool executed = false;
    {
        auto timer = loop->ScheduleRepeating(
                [&]() {
                    executed = true;
                    return false;
                },
                100ms, 0ms);
    }
    // Timer is out of scope and should be cancelled.
    loop->AdvanceClock(100ms);
    ASSERT_FALSE(executed);
}

TEST(TestEventLoop, ShutdownClearsPendingTasks) {
    auto loop = TestEventLoop::Create();
    bool executed = false;
    loop->Post([&]() { executed = true; }).IgnoreError();
    loop->Post([&]() { executed = true; }, 100ms).IgnoreError();

    loop->ShutdownAndWait().IgnoreError();
    loop->RunAll();
    loop->AdvanceClock(100ms);

    ASSERT_FALSE(executed);
}

// This test verifies that a repeating task can be executed multiple times
// without crashing. It directly targets the use-after-move bug in the
// faulty implementation.
TEST(TestEventLoop, RecurringTaskDoesNotCrashOnSubsequentExecutions) {
    // ARRANGE: Create an event loop and a counter.
    auto loop = TestEventLoop::Create();
    std::atomic<int> execution_count = 0;

    // Schedule a task to run every 10ms, starting immediately.
    auto timer = loop->ScheduleRepeating(
            [&execution_count]() {
                execution_count++;
                return true;
            },
            0ms,  // Initial delay of 0 means it's due immediately.
            10ms  // Repeat every 10ms.
    );

    // ACT & ASSERT (First Execution)
    // Advance the clock by 1ms. This should cause the task scheduled
    // at t=0 to run.
    loop->AdvanceClock(1ms);

    // With a correct implementation, the count is 1.
    // The faulty implementation would crash inside this AdvanceClock call.
    ASSERT_EQ(execution_count, 1);

    // ACT & ASSERT (Second Execution)
    // Advance the clock by another 10ms. This should cause the re-scheduled
    // task at t=10ms to run.
    loop->AdvanceClock(10ms);
    ASSERT_EQ(execution_count, 2);

    // ACT & ASSERT (Third Execution)
    // Advancing again proves the task continues to be rescheduled correctly.
    loop->AdvanceClock(10ms);
    ASSERT_EQ(execution_count, 3);
}

TEST(TestEventLoop, RunOne) {
    auto loop = TestEventLoop::Create();
    int count = 0;
    loop->Post([&]() { count++; }).IgnoreError();
    loop->Post([&]() { count++; }).IgnoreError();
    ASSERT_TRUE(loop->RunOne());
    ASSERT_EQ(1, count);
    ASSERT_TRUE(loop->RunOne());
    ASSERT_EQ(2, count);
}

TEST(TestEventLoop, RunMany) {
    auto loop = TestEventLoop::Create();
    int count = 0;
    loop->Post([&]() { count++; }).IgnoreError();
    loop->Post([&]() { count++; }).IgnoreError();
    loop->Post([&]() { count++; }).IgnoreError();
    ASSERT_TRUE(loop->RunMany(3));
    ASSERT_EQ(3, count);
}

TEST(TestEventLoop, RunManyTimeout) {
    auto loop = TestEventLoop::Create();
    int count = 0;
    loop->Post([&]() { count++; }).IgnoreError();
    ASSERT_EQ(loop->RunMany(2), 1);
    ASSERT_EQ(1, count);
}

TEST(TestEventLoop, RescheduleRepeatingTimer) {
    auto loop = TestEventLoop::Create();
    int counter = 0;

    auto handle = loop->ScheduleRepeating(
            [&]() {
                counter++;
                return true;
            },
            100ms, 100ms);

    // Let it fire once.
    loop->AdvanceClock(120ms);
    ASSERT_EQ(counter, 1);

    // Reschedule to fire sooner and more frequently.
    handle->Schedule(20ms, 20ms);

    // Check that it fires again quickly.
    loop->AdvanceClock(30ms);
    ASSERT_EQ(counter, 2);

    // Check that it fires again quickly.
    loop->AdvanceClock(15ms);
    ASSERT_EQ(counter, 3);
}
