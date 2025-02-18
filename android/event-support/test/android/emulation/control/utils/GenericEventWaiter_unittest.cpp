// Copyright (C) 2024 The Android Open Source Project
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
#include <gtest/gtest.h>

#include <thread>

#include "android/emulation/control/utils/EventWaiter.h"

namespace android {
namespace emulation {
namespace control {

// Test Event type (can be anything)
struct TestEvent {
    int value;
};

template <typename T>
struct TestEventPolicy {};

TEST(GenericEventWaiterTest, WaitForEvent) {
    EventChangeSupport<TestEvent> support;
    GenericEventWaiter<TestEvent> waiter(&support);

    TestEvent event = {.value = 123};
    support.fireEvent(event);

    EXPECT_TRUE(waiter.waitForNextEvent(absl::Milliseconds(10), 0));
}

TEST(GenericEventWaiterTest, WaitForEventTimeout) {
    EventChangeSupport<TestEvent> support;
    GenericEventWaiter<TestEvent> waiter(&support);

    EXPECT_FALSE(waiter.waitForNextEvent(absl::Milliseconds(10)));
}

TEST(GenericEventWaiterTest, WaitForEventAfterSequence) {
    EventChangeSupport<TestEvent> support;
    GenericEventWaiter<TestEvent> waiter(&support);

    TestEvent event1 = {.value = 1};
    support.fireEvent(event1);
    EXPECT_TRUE(waiter.waitForNextEvent(absl::Milliseconds(0), 0));

    TestEvent event2 = {.value = 2};
    support.fireEvent(event2);
    EXPECT_TRUE(waiter.waitForNextEvent(absl::Milliseconds(0), 1));
}

TEST(GenericEventWaiterTest, WaitForEventAfterSequenceTimeout) {
    EventChangeSupport<TestEvent> support;
    GenericEventWaiter<TestEvent> waiter(&support);

    TestEvent event = {.value = 123};
    support.fireEvent(event);

    EXPECT_FALSE(waiter.waitForNextEvent(absl::Milliseconds(0), 1));
}

TEST(GenericEventWaiterTest, MultithreadedEventWait) {
    EventChangeSupport<TestEvent> support;
    GenericEventWaiter<TestEvent> waiter(&support);

    std::atomic<bool> thread1Finished(false);
    std::atomic<bool> thread2Finished(false);

    std::thread thread1([&]() {
        EXPECT_TRUE(waiter.waitForNextEvent(absl::Milliseconds(500), 0));  // Expect an event
        thread1Finished = true;
    });

    std::thread thread2([&]() {
        TestEvent event = {.value = 456};
        absl::SleepFor(absl::Milliseconds(100));  // Short delay before firing event
        support.fireEvent(event);
        thread2Finished = true;
    });

    thread1.join();
    thread2.join();

    EXPECT_TRUE(thread1Finished);
    EXPECT_TRUE(thread2Finished);

    EXPECT_EQ(waiter.getEventSequence(), 1);  // Verify the event was processed once
}

TEST(GenericEventWaiterTest, MultithreadedEventSequenceWait) {
    EventChangeSupport<TestEvent> support;
    GenericEventWaiter<TestEvent> waiter(&support);

    std::atomic<bool> thread1Finished(false);
    std::atomic<bool> thread2Finished(false);
    std::atomic<bool> thread3Finished(false);

    std::thread thread1([&]() {
        EXPECT_TRUE(waiter.waitForNextEvent(absl::Milliseconds(100), 0));  // Expect event 1
        thread1Finished = true;
    });

    std::thread thread2([&]() {
        TestEvent event1 = {.value = 1};
        absl::SleepFor(absl::Milliseconds(20));  // Short delay before firing event 1
        support.fireEvent(event1);

        TestEvent event2 = {.value = 2};
        support.fireEvent(event2);

        thread2Finished = true;
    });

    std::thread thread3([&]() {
        EXPECT_TRUE(waiter.waitForNextEvent(absl::Milliseconds(100), 1));  // Expect event 2
        thread3Finished = true;
    });

    thread1.join();
    thread2.join();
    thread3.join();

    EXPECT_TRUE(thread1Finished);
    EXPECT_TRUE(thread2Finished);
    EXPECT_TRUE(thread3Finished);

    EXPECT_EQ(waiter.getEventSequence(), 2);  // Verify the events where processed
}

}  // namespace control
}  // namespace emulation
}  // namespace android