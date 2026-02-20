// Copyright 2025 The Android Open Source Project
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

#include <algorithm>
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "grpc_diagnostic.pb.h"

#include "android/status/status_matcher_macros.h"
#include "goldfish/circular_message_log.h"

namespace goldfish::proto_data_store {

using android::control::interceptor::GrpcBreadcrumb;

class CircularMessageLogTest : public ::testing::Test {
  protected:
    struct RawHeader {
        uint32_t magic;
        uint32_t head;
        uint32_t tail;
    };

    void SetUp() override { buffer_.assign(8 * 1024, 0); }

    RawHeader* GetHeader() { return reinterpret_cast<RawHeader*>(buffer_.data()); }

    std::vector<char> buffer_;
    GrpcBreadcrumb prototype_;
};

TEST_F(CircularMessageLogTest, BasicPushRead) {
    ASSERT_OK_AND_ASSIGN(
            auto log, CircularMessageLog::CreateWriter(buffer_.data(), buffer_.size(), prototype_));
    GrpcBreadcrumb crumb;
    crumb.set_call_id(1);
    ASSERT_OK(log->Push(crumb));

    int seen = 0;
    log->ForEach([&](const google::protobuf::Message& msg) {
        const auto& read_crumb = static_cast<const GrpcBreadcrumb&>(msg);
        if (read_crumb.call_id() == 1) seen++;
        return true;
    });
    EXPECT_EQ(seen, 1);
}

TEST_F(CircularMessageLogTest, UnalignedBuffer) {
    for (int i = 0; i < 8; ++i) {
        std::vector<char> raw_buffer(buffer_.size() + i);
        void* unaligned_ptr = raw_buffer.data() + i;

        ASSERT_OK_AND_ASSIGN(auto log, CircularMessageLog::CreateWriter(
                                               unaligned_ptr, buffer_.size(), prototype_));
        GrpcBreadcrumb crumb;
        crumb.set_call_id(42);
        ASSERT_OK(log->Push(crumb));

        int seen = 0;
        log->ForEach([&](const google::protobuf::Message& msg) {
            if (static_cast<const GrpcBreadcrumb&>(msg).call_id() == 42) seen++;
            return true;
        });
        EXPECT_EQ(seen, 1) << "Failed with alignment offset: " << i;
    }
}

TEST_F(CircularMessageLogTest, MessageCountAccuracy) {
    std::vector<char> small_buffer(32, 0);
    ASSERT_OK_AND_ASSIGN(auto log, CircularMessageLog::CreateWriter(
                                           small_buffer.data(), small_buffer.size(), prototype_));

    const int kNumMessages = 10;
    int i = 0;
    for (; i < kNumMessages; ++i) {
        GrpcBreadcrumb crumb;
        crumb.set_call_id(i + 1);
        ASSERT_OK(log->Push(crumb));

        int seen = 0;
        log->ForEach([&](const google::protobuf::Message&) {
            seen++;
            return true;
        });
        ASSERT_EQ(seen, log->MessageCount());
    }
}

TEST_F(CircularMessageLogTest, EmptyMessage) {
    ASSERT_OK_AND_ASSIGN(
            auto log, CircularMessageLog::CreateWriter(buffer_.data(), buffer_.size(), prototype_));

    GrpcBreadcrumb empty_crumb;
    ASSERT_OK(log->Push(empty_crumb));

    EXPECT_EQ(log->MessageCount(), 1);

    int seen = 0;
    log->ForEach([&](const google::protobuf::Message& msg) {
        const auto& read_crumb = static_cast<const GrpcBreadcrumb&>(msg);
        // An empty message should have its default values.
        EXPECT_EQ(read_crumb.call_id(), 0);
        EXPECT_TRUE(read_crumb.payload().empty());
        seen++;
        return true;
    });
    EXPECT_EQ(seen, 1);
}

TEST_F(CircularMessageLogTest, MagicSafety_PreventsGarbageCrash) {
    // 1. Fill buffer with garbage (non-zero, non-magic).
    std::memset(buffer_.data(), 0xAA, buffer_.size());

    // We should detect the garbage.
    auto log = CircularMessageLog::CreateReader(buffer_.data(), buffer_.size(), prototype_);
    EXPECT_TRUE(absl::IsNotFound(log.status())) << "Should return is not found";
}

TEST_F(CircularMessageLogTest, MagicSafety_PreservesExistingLog) {
    // 1. Create a log and push a message.
    {
        ASSERT_OK_AND_ASSIGN(auto log, CircularMessageLog::CreateWriter(
                                               buffer_.data(), buffer_.size(), prototype_));
        GrpcBreadcrumb crumb;
        crumb.set_call_id(123);
        ASSERT_OK(log->Push(crumb));
    }

    // 2. The memory now has a valid kMagic. Attach a Reader.
    ASSERT_OK_AND_ASSIGN(auto reader, CircularMessageLog::CreateReader(buffer_.data(),
                                                                       buffer_.size(), prototype_));

    // 3. Verify data is preserved.
    int seen = 0;
    reader->ForEach([&](const google::protobuf::Message& msg) {
        if (static_cast<const GrpcBreadcrumb&>(msg).call_id() == 123) seen++;
        return true;
    });
    EXPECT_EQ(seen, 1);
}

TEST_F(CircularMessageLogTest, CrashRecovery_UncommittedMessage) {
    ASSERT_OK_AND_ASSIGN(
            auto log, CircularMessageLog::CreateWriter(buffer_.data(), buffer_.size(), prototype_));

    GrpcBreadcrumb a;
    a.set_call_id(100);
    ASSERT_OK(log->Push(a));

    // Simulate a partial write (Message B) by omitting the CommitBit.
    const uint32_t head_before = GetHeader()->head;
    const uint16_t fake_len = 10;
    char* fake_data_ptr = buffer_.data() + CircularMessageLog::kHeaderSize + head_before;

    std::memcpy(fake_data_ptr, &fake_len, sizeof(fake_len));
    std::memset(fake_data_ptr + 2, 'B', fake_len);

    GetHeader()->head += (fake_len + 2);

    // Push valid message C.
    GrpcBreadcrumb c;
    c.set_call_id(300);
    ASSERT_OK(log->Push(c));

    std::vector<uint64_t> ids;
    log->ForEach([&](const google::protobuf::Message& msg) {
        ids.push_back(static_cast<const GrpcBreadcrumb&>(msg).call_id());
        return true;
    });

    ASSERT_EQ(ids.size(), 1);
    EXPECT_EQ(ids[0], 100);
}

TEST_F(CircularMessageLogTest, WrapAround_Eviction) {
    buffer_.assign(128, 0);
    ASSERT_OK_AND_ASSIGN(
            auto log, CircularMessageLog::CreateWriter(buffer_.data(), buffer_.size(), prototype_));

    std::string large_str(32, 'X');
    for (int i = 0; i < 20; ++i) {
        GrpcBreadcrumb crumb;
        crumb.set_call_id(i);
        crumb.set_payload(large_str);
        ASSERT_OK(log->Push(crumb));
    }

    uint32_t last_id = 0;
    int count = 0;
    log->ForEach([&](const google::protobuf::Message& msg) {
        last_id = static_cast<const GrpcBreadcrumb&>(msg).call_id();
        count++;
        return true;
    });

    EXPECT_EQ(last_id, 19);
    EXPECT_LT(count, 20);
    EXPECT_GT(count, 0);
}

TEST_F(CircularMessageLogTest, CorruptionResilience_InvalidLength) {
    ASSERT_OK_AND_ASSIGN(
            auto log, CircularMessageLog::CreateWriter(buffer_.data(), buffer_.size(), prototype_));

    const uint16_t evil_len = 0x7FFF;
    const uint16_t header = evil_len | 0x8000;

    std::memcpy(buffer_.data() + CircularMessageLog::kHeaderSize, &header, sizeof(header));
    GetHeader()->head = CircularMessageLog::kHeaderSize + 2 + evil_len;

    int seen = 0;
    log->ForEach([&](const google::protobuf::Message&) {
        seen++;
        return true;
    });

    EXPECT_EQ(seen, 0);
}

TEST_F(CircularMessageLogTest, ConcurrentStressTest) {
    ASSERT_OK_AND_ASSIGN(
            auto log, CircularMessageLog::CreateWriter(buffer_.data(), buffer_.size(), prototype_));
    const int kNumThreads = 8;
    const int kMessagesPerThread = 1000;
    std::vector<std::thread> threads;

    std::atomic<int> success_count{0};
    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([&, t]() {
            GrpcBreadcrumb crumb;
            for (int i = 0; i < kMessagesPerThread; ++i) {
                crumb.set_call_id(t * kMessagesPerThread + i);
                ASSERT_OK(log->Push(crumb));
                success_count++;
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(success_count, kNumThreads * kMessagesPerThread);
}

TEST_F(CircularMessageLogTest, EvictionBug_GapAtStart) {
    // This test exposes a bug in the eviction logic when a gap of uncommitted
    // memory exists before the message being evicted.

    // 1. Use a small buffer to make wrap-around predictable.
    // Header = 16B, Data Capacity = 112B.
    std::vector<uint64_t> ids;
    buffer_.assign(80, 0);
    ASSERT_OK_AND_ASSIGN(
            auto log, CircularMessageLog::CreateWriter(buffer_.data(), buffer_.size(), prototype_));

    GrpcBreadcrumb msg_b;
    msg_b.set_call_id(1);
    msg_b.set_payload(std::string(10, 'B'));
    ASSERT_OK(log->Push(msg_b));  // MSG B == 16 bytes. (14B payload + 2B header)

    GrpcBreadcrumb msg_c;
    msg_c.set_call_id(2);
    msg_c.set_payload(std::string(26, 'C'));
    ASSERT_OK(log->Push(msg_c));  // Message C (medium) == 32 Bytes
    // ASSERT_EQ(log->MessageCount(), 3);

    ids.clear();
    log->ForEach([&](const google::protobuf::Message& msg) {
        ids.push_back(static_cast<const GrpcBreadcrumb&>(msg).call_id());
        return true;
    });

    EXPECT_THAT(ids, testing::ElementsAre(1, 2));

    // 16 Bytes left.

    // Push message D, which is big enough to wrap and evict message A.
    // This creates a stale/uncommitted gap where A used to be.
    GrpcBreadcrumb msg_d;
    msg_d.set_call_id(3);
    msg_d.set_payload(std::string(10, 'D'));
    ASSERT_OK(log->Push(msg_d));

    ids.clear();
    log->ForEach([&](const google::protobuf::Message& msg) {
        ids.push_back(static_cast<const GrpcBreadcrumb&>(msg).call_id());
        return true;
    });

    // The final set of messages should not contain B (call_id 2).
    EXPECT_THAT(ids, testing::ElementsAre(1, 2, 3));
    // We now have 3 messages: B, C, D. Count should be 3.
    ASSERT_EQ(log->MessageCount(), 3);

    // Push a large message E that wraps and overwrites
    // the gap (where A was) AND message B.
    GrpcBreadcrumb msg_e;
    msg_e.set_call_id(4);
    msg_e.set_payload(std::string(16, 'E'));
    ASSERT_OK(log->Push(msg_e));

    ids.clear();
    log->ForEach([&](const google::protobuf::Message& msg) {
        ids.push_back(static_cast<const GrpcBreadcrumb&>(msg).call_id());
        return true;
    });

    // The final set of messages should not contain B (call_id 2).
    EXPECT_THAT(ids, testing::ElementsAre(3, 4));
}

TEST_F(CircularMessageLogTest, ForEachEarlyTermination) {
    ASSERT_OK_AND_ASSIGN(
            auto log, CircularMessageLog::CreateWriter(buffer_.data(), buffer_.size(), prototype_));

    for (int i = 0; i < 10; ++i) {
        GrpcBreadcrumb crumb;
        crumb.set_call_id(i);
        ASSERT_OK(log->Push(crumb));
    }

    int seen = 0;
    log->ForEach([&](const google::protobuf::Message& msg) {
        seen++;
        if (static_cast<const GrpcBreadcrumb&>(msg).call_id() == 5) {
            return false;  // Stop after seeing ID 5
        }
        return true;
    });

    // Should have seen IDs 0, 1, 2, 3, 4, 5 (total 6)
    EXPECT_EQ(seen, 6);
}

TEST_F(CircularMessageLogTest, ProtoCircularLog_Templated) {
    ASSERT_OK_AND_ASSIGN(auto log, ProtoCircularLog<GrpcBreadcrumb>::CreateWriter(buffer_.data(),
                                                                                  buffer_.size()));

    GrpcBreadcrumb crumb;
    crumb.set_call_id(999);
    ASSERT_OK(log->Push(crumb));

    int seen = 0;
    log->ForEach([&](const GrpcBreadcrumb& msg) {
        if (msg.call_id() == 999) seen++;
        return true;
    });
    EXPECT_EQ(seen, 1);
    EXPECT_EQ(log->MessageCount(), 1);
}

TEST_F(CircularMessageLogTest, PushExceedsCapacity) {
    // Header = 16B, Data Capacity = 64B.
    buffer_.assign(80, 0);
    ASSERT_OK_AND_ASSIGN(
            auto log, CircularMessageLog::CreateWriter(buffer_.data(), buffer_.size(), prototype_));

    // Try to push a message larger than 64 bytes.
    GrpcBreadcrumb large_crumb;
    large_crumb.set_payload(std::string(100, 'X'));
    auto status = log->Push(large_crumb);
    EXPECT_TRUE(absl::IsResourceExhausted(status));
}

TEST_F(CircularMessageLogTest, ProtoCircularLog_PushExceedsCapacity) {
    // Header = 16B, Data Capacity = 64B.
    buffer_.assign(80, 0);
    ASSERT_OK_AND_ASSIGN(auto log, ProtoCircularLog<GrpcBreadcrumb>::CreateWriter(buffer_.data(),
                                                                                  buffer_.size()));

    // Try to push a message larger than 64 bytes.
    GrpcBreadcrumb large_crumb;
    large_crumb.set_payload(std::string(100, 'X'));
    auto status = log->Push(large_crumb);
    EXPECT_TRUE(absl::IsResourceExhausted(status));
}

}  // namespace goldfish::proto_data_store
