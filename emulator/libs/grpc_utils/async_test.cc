// Copyright (C) 2023 The Android Open Source Project
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

#include <chrono>
#include <future>
#include <thread>

#include "absl/synchronization/notification.h"

#include "android/emulation/control/library.h"

// Global variable to keep track of destructor calls
namespace {
int global_destructor_calls = 0;
}  // namespace

using android::emulation::control::Library;

// Custom object that increments the global variable when destructed
struct CustomObject {
    CustomObject() = default;
    ~CustomObject() { global_destructor_calls++; }

    void Nothing() { /* do nothing.. */ }
};

TEST(LibraryTest, BorrowFromEmptyLibrary) {
    Library<int> my_library;
    auto obj = my_library.Acquire();

    // We only have one use count, and that's us
    EXPECT_EQ(obj.use_count(), 1);
}

TEST(LibraryTest, ReturnObjectMultipleTimes) {
    Library<int> my_library;
    auto obj = my_library.Acquire();

    obj.reset();
    EXPECT_EQ(obj.use_count(), 0);

    obj.reset();  // Repeated reset should not cause issues
    EXPECT_EQ(obj.use_count(), 0);
}

TEST(LibraryTest, CustomObjectDestruction) {
    Library<CustomObject> my_library;

    global_destructor_calls = 0;
    // Borrow a CustomObject
    auto obj1 = my_library.Acquire();

    // Ensure the global_destructor_calls is not incremented yet
    EXPECT_EQ(global_destructor_calls, 0);

    // Return the CustomObject by letting obj1 go out of scope
    // This should increment global_destructor_calls
    obj1.reset();

    // Ensure the global_destructor_calls is incremented after returning
    EXPECT_EQ(global_destructor_calls, 1);
}

TEST(LibraryTest, ForeachInvocation) {
    Library<CustomObject> my_library;

    auto obj1 = my_library.Acquire();
    {
        // We now have 2 borrowed objects..
        auto obj2 = my_library.Acquire();

        // So we should iterate over two objects!
        int count = 0;
        my_library.ForEach([&count](auto o) {
            o->Nothing();
            count++;
        });

        EXPECT_EQ(count, 2);
    }

    // We returned an object, so iterating over them should result in only one
    // callback function being invoked.
    int count = 0;
    my_library.ForEach([&count](auto o) {
        o->Nothing();
        count++;
    });

    EXPECT_EQ(count, 1);
}

TEST(LibraryTest, ConcurrencyTest) {
    constexpr int kNumThreads = 4;
    constexpr int kNumIterationsPerThread = 1000;
    Library<int> my_library;

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);
    std::atomic<int> counter(0);

    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < kNumIterationsPerThread; ++j) {
                auto obj = my_library.Acquire();
                // Do some work with the borrowed object
                counter.fetch_add(1, std::memory_order_relaxed);

                // Check that the library is not empty.
                int count = 0;
                my_library.ForEach([&count](auto /*o*/) { count++; });
                EXPECT_GE(count, 1);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Ensure the counter matches the expected total number of borrows
    EXPECT_EQ(counter.load(std::memory_order_relaxed), kNumThreads * kNumIterationsPerThread);

    // Check that the library is empty.
    int count = 0;
    my_library.ForEach([&count](auto /*o*/) { count++; });
    EXPECT_EQ(count, 0);
}

TEST(LibraryTest, WaitForEmptyLibraryTestTimesOut) {
    Library<int> my_library;
    auto item = my_library.Acquire(5);
    EXPECT_FALSE(my_library.WaitUntilLibraryIsClear(std::chrono::milliseconds(10)));
}

TEST(LibraryTest, WaitForEmptyLibraryTestWaitsUntilFinished) {
    using namespace std::chrono_literals;

    Library<int> my_library;
    absl::Notification item_acquired;
    absl::Notification item_release;

    std::thread worker_thread([&] {
        // Acquire an item, making the library non-empty.
        auto item = my_library.Acquire();

        // SIGNAL 1: Tell the main thread that the item has been acquired.
        item_acquired.Notify();

        // Wait until the main thread explicitly tells us to release the item.
        item_release.WaitForNotification();
    });

    // Wait until the worker acquired the item.
    item_acquired.WaitForNotification();

    // Call WaitUntilLibraryIsClear in an asynchronous task.
    // This will block at most 500ms..
    auto wait_future = std::async(std::launch::async,
                                  [&] { return my_library.WaitUntilLibraryIsClear(500ms); });

    // Tell the worker thread to release the item
    item_release.Notify();

    // The item should have been returned so the wait should return true.
    EXPECT_TRUE(wait_future.get())
            << "The library wait timed out, and items were still present after 500ms";

    worker_thread.join();
}
