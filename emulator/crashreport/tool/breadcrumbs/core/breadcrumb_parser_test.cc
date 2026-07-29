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
#include "android/crashreport/breadcrumbs/breadcrumb_parser.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "android/crashreport/breadcrumb_proto.h"
#include "breadcrumb.pb.h"
#include "goldfish/raw_circular_log.h"

namespace android::crashreport::breadcrumbs {

using android::control::breadcrumbs::Breadcrumb;
using android::crashreport::BreadcrumbEnvelope;
using android::crashreport::BreadcrumbPhase;
using android::crashreport::PayloadType;
using android::crashreport::RawAdbPayload;
using goldfish::proto_data_store::RawCircularLog;

TEST(BreadcrumbParserTest, ReturnsEmptyOnTooSmallBuffer) {
    std::vector<uint8_t> buffer(5);  // Less than RawCircularLog::kHeaderSize
    auto entries = BreadcrumbParser::Parse(buffer);
    EXPECT_TRUE(entries.empty());
}

TEST(BreadcrumbParserTest, ReturnsEmptyOnCorruptedLog) {
    std::vector<uint8_t> buffer(100, 0xFF);  // Random data
    auto entries = BreadcrumbParser::Parse(buffer);
    EXPECT_TRUE(entries.empty());
}

TEST(BreadcrumbParserTest, ParsesAdbRawPayload) {
    // 1. Create a buffer for the log
    std::vector<uint8_t> buffer(1024);
    auto log_status = RawCircularLog::CreateWriter(buffer.data(), buffer.size());
    ASSERT_TRUE(log_status.ok());
    auto log = std::move(*log_status);

    // 2. Push a raw ADB event
    BreadcrumbEnvelope envelope;
    envelope.timestamp_ns = 12345;
    envelope.thread_id = 6789;
    envelope.flow_id = 42;
    envelope.phase = BreadcrumbPhase::kInstant;
    envelope.payload_type = PayloadType::kAdbRawToGuest;

    RawAdbPayload adb_payload;
    adb_payload.command = 0x4e584e43;  // 'CNXN'
    adb_payload.snippet_len = 4;

    uint16_t payload_len = sizeof(RawAdbPayload) + 4;
    envelope.payload_len = payload_len;

    uint32_t total_size = sizeof(BreadcrumbEnvelope) + payload_len;

    auto status = log->Push(total_size, [&](void* dest) {
        char* p = static_cast<char*>(dest);
        std::memcpy(p, &envelope, sizeof(BreadcrumbEnvelope));
        p += sizeof(BreadcrumbEnvelope);
        std::memcpy(p, &adb_payload, sizeof(RawAdbPayload));
        p += sizeof(RawAdbPayload);
        std::memcpy(p, "SYNC", 4);
    });
    ASSERT_TRUE(status.ok());

    // 3. Parse the buffer
    auto entries = BreadcrumbParser::Parse(buffer);

    // 4. Verify
    ASSERT_EQ(entries.size(), 1);
    const auto& event = entries[0];
    EXPECT_EQ(event.timestamp_ns(), 12345);
    EXPECT_EQ(event.thread_id(), 6789);
    EXPECT_EQ(event.flow_id(), 42);
    EXPECT_EQ(event.phase(), Breadcrumb::INSTANT);

    ASSERT_TRUE(event.has_adb());
    EXPECT_EQ(event.adb().command(), 0x4e584e43);
    EXPECT_EQ(event.adb().direction(), android::control::breadcrumbs::AdbPayload::TO_GUEST);
    EXPECT_EQ(event.adb().data_snippet(), "SYNC");
}

TEST(BreadcrumbParserTest, ParsesGrpcProtoPayload) {
    using android::control::breadcrumbs::GrpcPayload;

    std::vector<uint8_t> buffer(1024);
    auto log_status = RawCircularLog::CreateWriter(buffer.data(), buffer.size());
    ASSERT_TRUE(log_status.ok());
    auto log = std::move(*log_status);

    GrpcPayload grpc_data;
    grpc_data.set_method_hash(12345678);
    grpc_data.set_payload("bar");
    std::string serialized = grpc_data.SerializeAsString();

    BreadcrumbEnvelope envelope;
    envelope.timestamp_ns = 99999;
    envelope.thread_id = 1111;
    envelope.flow_id = 2222;
    envelope.phase = BreadcrumbPhase::kFlowStep;
    envelope.payload_type = PayloadType::kGrpcProto;
    envelope.payload_len = static_cast<uint16_t>(serialized.size());

    uint32_t total_size = sizeof(BreadcrumbEnvelope) + serialized.size();
    auto status = log->Push(total_size, [&](void* dest) {
        char* p = static_cast<char*>(dest);
        std::memcpy(p, &envelope, sizeof(BreadcrumbEnvelope));
        p += sizeof(BreadcrumbEnvelope);
        std::memcpy(p, serialized.data(), serialized.size());
    });
    ASSERT_TRUE(status.ok());

    auto entries = BreadcrumbParser::Parse(buffer);
    ASSERT_EQ(entries.size(), 1);
    const auto& event = entries[0];
    EXPECT_EQ(event.timestamp_ns(), 99999);
    EXPECT_EQ(event.phase(), Breadcrumb::FLOW_STEP);
    ASSERT_TRUE(event.has_grpc());
    EXPECT_EQ(event.grpc().method_hash(), 12345678);
    EXPECT_EQ(event.grpc().payload(), "bar");
}

TEST(BreadcrumbParserTest, SkipsTooSmallRecord) {
    std::vector<uint8_t> buffer(1024);
    auto log_status = RawCircularLog::CreateWriter(buffer.data(), buffer.size());
    ASSERT_TRUE(log_status.ok());
    auto log = std::move(*log_status);

    // Push an invalid tiny object (smaller than BreadcrumbEnvelope)
    auto status = log->Push(10, [&](void* dest) { std::memset(dest, 0, 10); });
    ASSERT_TRUE(status.ok());

    auto entries = BreadcrumbParser::Parse(buffer);
    EXPECT_TRUE(entries.empty());
}

TEST(BreadcrumbParserTest, SkipsRecordWithTooLargePayloadSize) {
    std::vector<uint8_t> buffer(1024);
    auto log_status = RawCircularLog::CreateWriter(buffer.data(), buffer.size());
    ASSERT_TRUE(log_status.ok());
    auto log = std::move(*log_status);

    BreadcrumbEnvelope envelope;
    envelope.timestamp_ns = 5555;
    envelope.payload_type = PayloadType::kRaw;
    // Claim a huge payload size that exceeds the pushed object size
    envelope.payload_len = 500;

    uint32_t total_size = sizeof(BreadcrumbEnvelope) + 10;  // pushed size is small
    auto status = log->Push(total_size, [&](void* dest) {
        std::memcpy(dest, &envelope, sizeof(BreadcrumbEnvelope));
    });
    ASSERT_TRUE(status.ok());

    auto entries = BreadcrumbParser::Parse(buffer);
    ASSERT_EQ(entries.size(), 1);
    // Verify that the envelope was parsed but the payload was skipped
    EXPECT_EQ(entries[0].timestamp_ns(), 5555);
    EXPECT_FALSE(entries[0].has_adb());
    EXPECT_FALSE(entries[0].has_grpc());
}

TEST(BreadcrumbParserTest, ParsesWrappedCircularBuffer) {
    std::vector<uint8_t> buffer(256);  // Force wrap-around easily
    auto log_status = RawCircularLog::CreateWriter(buffer.data(), buffer.size());
    ASSERT_TRUE(log_status.ok());
    auto log = std::move(*log_status);

    // Push 10 entries. Since buffer is 256 bytes and each entry is 32 bytes
    // (sizeof(BreadcrumbEnvelope)), it will wrap around and overwrite older entries.
    for (int i = 0; i < 10; ++i) {
        BreadcrumbEnvelope envelope;
        envelope.timestamp_ns = 1000 + i;
        envelope.thread_id = 100 + i;
        envelope.flow_id = i;
        envelope.phase = BreadcrumbPhase::kInstant;
        envelope.payload_type = PayloadType::kRaw;
        envelope.payload_len = 0;

        uint32_t total_size = sizeof(BreadcrumbEnvelope);
        auto status = log->Push(total_size, [&](void* dest) {
            std::memcpy(dest, &envelope, sizeof(BreadcrumbEnvelope));
        });
        ASSERT_TRUE(status.ok());
    }

    auto entries = BreadcrumbParser::Parse(buffer);
    EXPECT_GT(entries.size(), 0);
    EXPECT_LT(entries.size(), 10);  // Assure wrap-around eviction happened

    // Verify chronological ordering of the remaining parsed entries
    for (size_t i = 1; i < entries.size(); ++i) {
        EXPECT_GT(entries[i].timestamp_ns(), entries[i - 1].timestamp_ns());
    }
}

TEST(BreadcrumbParserTest, ParsesLooperPostContextRawPayload) {
    using android::control::breadcrumbs::LooperPayload;
    using android::crashreport::RawLooperPostWithContextPayload;

    std::vector<uint8_t> buffer(1024);
    auto log_status = RawCircularLog::CreateWriter(buffer.data(), buffer.size());
    ASSERT_TRUE(log_status.ok());
    auto log = std::move(*log_status);

    BreadcrumbEnvelope envelope;
    envelope.timestamp_ns = 33333;
    envelope.thread_id = 4444;
    envelope.flow_id = 5555;
    envelope.phase = BreadcrumbPhase::kFlowBegin;
    envelope.payload_type = PayloadType::kLooperPostContextRaw;

    uint64_t fake_pc = 0xabcdef01;
    RawLooperPostWithContextPayload looper_payload;
    looper_payload.caller_pc = StackAddress(fake_pc);
    looper_payload.loop_id = 99;
    looper_payload.context_len = 5;

    uint16_t payload_len = sizeof(RawLooperPostWithContextPayload) + 5;
    envelope.payload_len = payload_len;

    uint32_t total_size = sizeof(BreadcrumbEnvelope) + payload_len;

    auto status = log->Push(total_size, [&](void* dest) {
        char* p = static_cast<char*>(dest);
        std::memcpy(p, &envelope, sizeof(BreadcrumbEnvelope));
        p += sizeof(BreadcrumbEnvelope);
        std::memcpy(p, &looper_payload, sizeof(RawLooperPostWithContextPayload));
        p += sizeof(RawLooperPostWithContextPayload);
        std::memcpy(p, "hello", 5);
    });
    ASSERT_TRUE(status.ok());

    auto entries = BreadcrumbParser::Parse(buffer);
    ASSERT_EQ(entries.size(), 1);
    const auto& event = entries[0];
    EXPECT_EQ(event.timestamp_ns(), 33333);
    EXPECT_EQ(event.phase(), Breadcrumb::FLOW_BEGIN);

    ASSERT_TRUE(event.has_looper());
    EXPECT_EQ(event.looper().event(), LooperPayload::POST);
    EXPECT_EQ(event.looper().caller_pc(), StackAddress(fake_pc));
    EXPECT_EQ(event.looper().loop_id(), 99);
    EXPECT_EQ(event.looper().context(), "hello");
}

TEST(BreadcrumbParserTest, ParsesLooperExecRawPayload) {
    using android::control::breadcrumbs::LooperPayload;
    using android::crashreport::RawLooperExecPayload;

    std::vector<uint8_t> buffer(1024);
    auto log_status = RawCircularLog::CreateWriter(buffer.data(), buffer.size());
    ASSERT_TRUE(log_status.ok());
    auto log = std::move(*log_status);

    BreadcrumbEnvelope envelope;
    envelope.timestamp_ns = 44444;
    envelope.thread_id = 5555;
    envelope.flow_id = 6666;
    envelope.phase = BreadcrumbPhase::kFlowEnd;
    envelope.payload_type = PayloadType::kLooperExecRaw;

    RawLooperExecPayload looper_payload;
    looper_payload.loop_id = 88;

    uint16_t payload_len = sizeof(RawLooperExecPayload);
    envelope.payload_len = payload_len;

    uint32_t total_size = sizeof(BreadcrumbEnvelope) + payload_len;

    auto status = log->Push(total_size, [&](void* dest) {
        char* p = static_cast<char*>(dest);
        std::memcpy(p, &envelope, sizeof(BreadcrumbEnvelope));
        p += sizeof(BreadcrumbEnvelope);
        std::memcpy(p, &looper_payload, sizeof(RawLooperExecPayload));
    });
    ASSERT_TRUE(status.ok());

    auto entries = BreadcrumbParser::Parse(buffer);
    ASSERT_EQ(entries.size(), 1);
    const auto& event = entries[0];
    EXPECT_EQ(event.timestamp_ns(), 44444);
    EXPECT_EQ(event.phase(), Breadcrumb::FLOW_END);

    ASSERT_TRUE(event.has_looper());
    EXPECT_EQ(event.looper().event(), LooperPayload::EXECUTE);
    EXPECT_EQ(event.looper().loop_id(), 88);
}

}  // namespace android::crashreport::breadcrumbs
