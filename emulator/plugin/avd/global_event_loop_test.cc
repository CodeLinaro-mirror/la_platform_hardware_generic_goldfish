// Copyright 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "goldfish/async/testing/global_event_loop.h"

#include <gtest/gtest.h>

#include <future>
#include <thread>
#include <vector>

#include "goldfish/async/event_loop.h"

namespace goldfish {
namespace async {

TEST(QemuLooperTest, IsNotNull) {
    // Verifies that the global event loop is not null.
    EXPECT_NE(nullptr, globalEventLoop());
}

TEST(QemuLooperTest, ReturnsSameInstance) {
    // Verifies that subsequent calls return the same instance.
    auto* loop1 = globalEventLoop();
    auto* loop2 = globalEventLoop();
    EXPECT_EQ(loop1, loop2);
}

TEST(QemuLooperTest, IsThreadSafe) {
    // Verifies that the same instance is returned across multiple threads.
    auto* firstLoop = globalEventLoop();
    EXPECT_NE(nullptr, firstLoop);

    constexpr int kNumThreads = 10;
    std::vector<std::future<::goldfish::async::EventLoop*>> futures;
    for (int i = 0; i < kNumThreads; ++i) {
        futures.push_back(std::async(std::launch::async, [] { return globalEventLoop(); }));
    }

    for (size_t i = 0; i < futures.size(); ++i) {
        EXPECT_EQ(firstLoop, futures[i].get());
    }
}

}  // namespace async
}  // namespace goldfish
