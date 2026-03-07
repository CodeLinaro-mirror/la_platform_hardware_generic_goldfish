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
#include "goldfish/async/scoped_async_resource.h"

#include <gtest/gtest.h>

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/testing/fake_async_socket.h"
#include "goldfish/async/threaded_event_loop.h"

namespace goldfish::async {

class ScopedAsyncResourceTest : public ::testing::Test {
  protected:
    void SetUp() override { loop_ = ThreadedEventLoop::Create(LibuvEventLoop::Create()); }

    std::unique_ptr<ThreadedEventLoop> loop_;
};

TEST_F(ScopedAsyncResourceTest, MoveAssignmentOnLoopThread) {
    auto socket1 = std::make_shared<testing::FakeAsyncSocket>();
    socket1->setEventLoop(loop_.get());

    auto socket2 = std::make_shared<testing::FakeAsyncSocket>();
    socket2->setEventLoop(loop_.get());

    // Before the fix, this would FATAL crash because ScopedAsyncResource::operator=
    // called PostAndWait() while already on the loop thread.
    auto status = loop_->PostAndWait([&]() {
        ScopedAsyncResource<AsyncSocket> scoped(socket1);

        // This move assignment triggers the problematic code path.
        scoped = ScopedAsyncResource<AsyncSocket>(socket2);

        return absl::OkStatus();
    });

    EXPECT_TRUE(status.ok());
}

TEST_F(ScopedAsyncResourceTest, DestructorOnLoopThread) {
    auto socket = std::make_shared<testing::FakeAsyncSocket>();
    socket->setEventLoop(loop_.get());

    auto status = loop_->PostAndWait([&]() {
        // Destructor also checks IsOnLoopThread
        ScopedAsyncResource<AsyncSocket> scoped(socket);
        return absl::OkStatus();
    });

    EXPECT_TRUE(status.ok());
}

}  // namespace goldfish::async
