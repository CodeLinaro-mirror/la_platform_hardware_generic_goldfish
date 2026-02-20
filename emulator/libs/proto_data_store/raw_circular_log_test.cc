// Copyright 2026 The Android Open Source Project
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

#include "goldfish/raw_circular_log.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"

#include "android/status/status_matcher_macros.h"

namespace goldfish::proto_data_store {

class RawCircularLogTest : public ::testing::Test {
  protected:
    void SetUp() override { buffer_.assign(128, 0); }
    std::vector<char> buffer_;
};

TEST_F(RawCircularLogTest, CreateWriterInitializesMagic) {
    ASSERT_OK_AND_ASSIGN(auto log, RawCircularLog::CreateWriter(buffer_.data(), buffer_.size()));
    uint32_t magic;
    std::memcpy(&magic, buffer_.data(), sizeof(magic));
    EXPECT_EQ(magic, RawCircularLog::kMagic);
}

TEST_F(RawCircularLogTest, BasicPushAndTraverse) {
    ASSERT_OK_AND_ASSIGN(auto log, RawCircularLog::CreateWriter(buffer_.data(), buffer_.size()));

    // Use the new Push API which handles the commit protocol.
    ASSERT_OK(log->Push(10, [&](void* data_ptr) { std::memcpy(data_ptr, "1234567890", 10); }));

    int seen = 0;
    log->ForEach([&](void* data, uint16_t size) {
        EXPECT_EQ(size, 10);
        EXPECT_EQ(std::string_view(static_cast<char*>(data), 10), "1234567890");
        seen++;
        return true;
    });
    EXPECT_EQ(seen, 1);
}

TEST_F(RawCircularLogTest, EvictionBug_GapAtStart) {
    // 1. Use a small buffer to make wrap-around predictable.
    // Header = 16B, Data Capacity = 64B.
    buffer_.assign(80, 0);
    ASSERT_OK_AND_ASSIGN(auto log, RawCircularLog::CreateWriter(buffer_.data(), buffer_.size()));

    auto push_raw = [&](uint32_t size, char fill) {
        ASSERT_OK(log->Push(size, [&](void* data_ptr) { std::memset(data_ptr, fill, size); }));
    };

    push_raw(14, 'B');  // 14 + 2 = 16 bytes.
    push_raw(30, 'C');  // 30 + 2 = 32 bytes.

    std::vector<uint16_t> sizes;
    log->ForEach([&](void*, uint16_t size) {
        sizes.push_back(size);
        return true;
    });
    EXPECT_THAT(sizes, testing::ElementsAre(14, 30));

    // 16 Bytes left.
    // Push message D (14 + 2 = 16 bytes), which is big enough to fill but not wrap.
    push_raw(14, 'D');

    sizes.clear();
    log->ForEach([&](void*, uint16_t size) {
        sizes.push_back(size);
        return true;
    });
    EXPECT_THAT(sizes, testing::ElementsAre(14, 30, 14));
    EXPECT_EQ(log->ObjectCount(), 3);

    // Push message E (38 + 2 = 40 bytes). This MUST wrap and evict 'B' and 'C'.
    // Memory map:
    // [0, 16): B (evicted)
    // [16, 48): C (evicted)
    // [48, 64): D (kept)
    // [64, 68): Gap (evicted)
    // [0, 40): E (new)
    push_raw(38, 'E');

    sizes.clear();
    log->ForEach([&](void*, uint16_t size) {
        sizes.push_back(size);
        return true;
    });
    // Should have evicted 'B' and 'C' but kept 'D' (14) and added 'E' (38).
    // Order: D, E
    EXPECT_THAT(sizes, testing::ElementsAre(14, 38));
}

TEST_F(RawCircularLogTest, PushExceedsCapacity) {
    // Data capacity = 128 - 16 = 112 bytes.
    ASSERT_OK_AND_ASSIGN(auto log, RawCircularLog::CreateWriter(buffer_.data(), buffer_.size()));

    // Try to push 113 bytes (exceeds 112).
    auto status = log->Push(113, [](void*) {});
    EXPECT_TRUE(absl::IsResourceExhausted(status));
}

}  // namespace goldfish::proto_data_store
