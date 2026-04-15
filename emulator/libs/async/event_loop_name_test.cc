// Copyright (C) 2026 The Android Open Source Project
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

#include <future>
#include <string>
#include <thread>

#include "android/base/threads/thread_utils.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"

namespace goldfish::async {

TEST(EventLoopNameTest, LibuvLoopStoresName) {
    auto loop = LibuvEventLoop::Create("MyLibuvLoop");
    EXPECT_EQ(loop->GetName(), "MyLibuvLoop");
}

TEST(EventLoopNameTest, TestLoopStoresName) {
    auto loop = testing::TestEventLoop::Create("MyTestLoop");
    EXPECT_EQ(loop->GetName(), "MyTestLoop");
}

TEST(EventLoopNameTest, ThreadedLoopAdoptsName) {
    auto inner = LibuvEventLoop::Create("InnerLoop");
    auto threaded = ThreadedEventLoop::Create(std::move(inner));
    EXPECT_EQ(threaded->GetName(), "InnerLoop");
}

TEST(EventLoopNameTest, ThreadedLoopRenamesThread) {
    const std::string name = "ThreadedName";
    auto inner = LibuvEventLoop::Create(name);
    auto threaded = ThreadedEventLoop::Create(std::move(inner));

    auto result = threaded->PostAndWait(
            [&]() { return android::base::ThreadUtils::GetCurrentThreadName(); });

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), name);

    (void)threaded->ShutdownAndWait();
}

TEST(EventLoopNameTest, TestLoopRenamesThread) {
    const std::string name = "TestLoopName";
    auto loop = testing::TestEventLoop::Create(name);

    std::string captured_name;
    (void)loop->Post([&]() { captured_name = android::base::ThreadUtils::GetCurrentThreadName(); });

    // TestEventLoop requires explicit execution commands.
    loop->RunOne();

    EXPECT_EQ(captured_name, name);

    (void)loop->ShutdownAndWait();
}

}  // namespace goldfish::async
