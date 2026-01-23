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

#include "goldfish/async/when_all.h"

#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include "absl/status/status_matchers.h"
#include "gtest/gtest.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/testing/test_event_loop.h"

using namespace goldfish::async;
using namespace goldfish::async::testing;

TEST(WhenAll, TestEventLoop) {
    auto loop = TestEventLoop::create();

    std::pair<int, int> result = {};

    loop->Post([&]() {
            auto the_pair = std::make_shared<WhenAll<std::pair<int, int>>>(
                    loop.get(), [&result](std::pair<int, int> p) { result = p; });

            loop->Post([the_pair]() { the_pair->MutableResults().first = 42; }).IgnoreError();

            loop->Post([the_pair]() { the_pair->MutableResults().second = 67; }).IgnoreError();
        }).IgnoreError();

    loop->runAll();

    EXPECT_EQ(result.first, 42);
    EXPECT_EQ(result.second, 67);
}

TEST(WhenAll, LibuvEventLoop) {
    auto loop = LibuvEventLoop::Create();
    std::thread loop_thread([&loop] { loop->Run().IgnoreError(); });

    std::promise<std::pair<int, int>> pair_promise;
    std::future<std::pair<int, int>> future_result = pair_promise.get_future();

    loop->Post([&]() {
            auto the_pair = std::make_shared<WhenAll<std::pair<int, int>>>(
                    loop.get(), [&pair_promise](std::pair<int, int> p) {
                        pair_promise.set_value(std::move(p));
                    });

            loop->Post([the_pair]() { the_pair->MutableResults().first = 42; }).IgnoreError();

            loop->Post([the_pair]() { the_pair->MutableResults().second = 67; }).IgnoreError();
        }).IgnoreError();

    const std::pair<int, int> result = future_result.get();

    EXPECT_EQ(result.first, 42);
    EXPECT_EQ(result.second, 67);

    using namespace std::literals::chrono_literals;

    ASSERT_THAT(loop->ShutdownAndWait(100ms), absl_testing::IsOk());
    ASSERT_TRUE(loop_thread.joinable());
    loop_thread.join();
}