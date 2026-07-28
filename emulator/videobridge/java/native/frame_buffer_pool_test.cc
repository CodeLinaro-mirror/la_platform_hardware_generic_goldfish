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

#include "frame_buffer_pool.h"

#include <gtest/gtest.h>

namespace goldfish::videobridge::java {

TEST(FrameBufferPoolTest, AcquireAndReleaseBuffer) {
    auto pool = std::make_shared<FrameBufferPool>(2);

    auto buf1 = pool->Acquire(1024);
    ASSERT_NE(buf1, nullptr);
    EXPECT_GE(buf1->capacity(), 1024u);

    auto buf2 = pool->Acquire(2048);
    ASSERT_NE(buf2, nullptr);
    EXPECT_NE(buf1->id(), buf2->id());

    uint64_t original_id1 = buf1->id();
    buf1->Release();

    // Re-acquire should reuse buf1 from the pool
    auto buf3 = pool->Acquire(512);
    ASSERT_NE(buf3, nullptr);
    EXPECT_EQ(buf3->id(), original_id1);
}

TEST(FrameBufferPoolTest, AutoExpandCapacity) {
    auto pool = std::make_shared<FrameBufferPool>(1);

    auto buf = pool->Acquire(1024);
    ASSERT_NE(buf, nullptr);
    EXPECT_GE(buf->capacity(), 1024u);

    buf->EnsureCapacity(4096);
    EXPECT_GE(buf->capacity(), 4096u);
}

TEST(FrameBufferPoolTest, WeakPtrSafetyWhenPoolDestroyed) {
    uint64_t buf_id = 0;
    std::shared_ptr<NativeFrameBuffer> buf;
    {
        auto pool = std::make_shared<FrameBufferPool>(1);
        buf = pool->Acquire(1024);
        ASSERT_NE(buf, nullptr);
        buf_id = buf->id();
    }
    EXPECT_GT(buf_id, 0u);
    // pool is now destroyed, buf is still alive.
    // Release should safely no-op instead of UAF
    EXPECT_NO_FATAL_FAILURE(buf->Release());
}

TEST(FrameBufferPoolTest, MultipleAcquireAndRecycleCycle) {
    auto pool = std::make_shared<FrameBufferPool>(4);

    std::vector<std::shared_ptr<NativeFrameBuffer>> buffers;
    for (int i = 0; i < 10; ++i) {
        buffers.push_back(pool->Acquire(100));
    }

    for (auto& b : buffers) {
        b->Release();
    }

    // Next 10 acquires should reuse the 10 pooled buffers
    for (int i = 0; i < 10; ++i) {
        auto reacquired = pool->Acquire(100);
        EXPECT_NE(reacquired, nullptr);
    }
}

}  // namespace goldfish::videobridge::java
