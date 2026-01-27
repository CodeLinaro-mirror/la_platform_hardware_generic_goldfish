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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <thread>

#include "goldfish/eventing/property_change_support.h"

namespace android::base {
TEST(PropertyChangeEventTest, InitialValue) {
    const PropertyChangeEvent<int> event(10);
    EXPECT_EQ(event.Last(), 10);
    EXPECT_EQ(event.Prev(), 0);  // Default initialized
}

TEST(PropertyChangeEventTest, UpdateValue) {
    PropertyChangeEvent<std::string> event;
    event.Update("first");
    EXPECT_EQ(event.Last(), "first");
    EXPECT_EQ(event.Prev(), "");  // Default initialized

    event.Update("second");
    EXPECT_EQ(event.Last(), "second");
    EXPECT_EQ(event.Prev(), "first");

    event.Update("third");
    EXPECT_EQ(event.Last(), "third");
    EXPECT_EQ(event.Prev(), "second");
}

TEST(PropertyChangeEventTest, UpdateValueSame) {
    PropertyChangeEvent<std::string> event("initial");
    event.Update("initial");
    EXPECT_EQ(event.Last(), "initial");
    EXPECT_EQ(event.Prev(), "initial");
}

TEST(PropertyChangeEventTest, ThreadSafetyExample) {
    PropertyChangeEvent<int> event;

    auto update_thread = [&](int new_value) { event.Update(new_value); };

    std::vector<std::thread> threads;
    threads.reserve(100);
    for (int i = 0; i < 100; ++i) {
        threads.emplace_back(update_thread, i % 2);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_THAT(event.Last(), ::testing::AnyOf(0, 1));
}
}  // namespace android::base
