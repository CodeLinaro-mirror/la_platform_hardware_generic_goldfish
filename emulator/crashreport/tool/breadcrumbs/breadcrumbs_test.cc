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
#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <vector>

#include "android/crashreport/breadcrumbs/breadcrumb_parser.h"
#include "car_service.pb.h"
#include "emulator/crashreport/include/android/crashreport/breadcrumb_proto.h"
#include "emulator/crashreport/tool/breadcrumbs/breadcrumb_metadata.h"
#include "emulator_controller.pb.h"
#include "sensor_service.pb.h"

namespace android::crashreport::breadcrumbs {

TEST(BreadcrumbParserTest, ParsesEmptyBuffer) {
    std::vector<uint8_t> buffer(64, 0);
    auto entries = BreadcrumbParser::Parse(buffer);
    EXPECT_TRUE(entries.empty());
}

using android::control::breadcrumbs::Breadcrumb;

TEST(BreadcrumbParserTest, ParsesContiguousBuffer) {
    using android::control::breadcrumbs::Breadcrumb;
    using android::control::breadcrumbs::GrpcPayload;
    using android::crashreport::BreadcrumbEnvelope;
    using android::crashreport::BreadcrumbPhase;
    using android::crashreport::PayloadType;
    using goldfish::proto_data_store::RawCircularLog;

    std::vector<uint8_t> buffer(1024, 0);

    GrpcPayload grpc;
    grpc.set_method_hash(0xABCDEF);
    grpc.set_grpc_phase(GrpcPayload::START);
    grpc.set_status_code(GrpcPayload::OK);

    uint16_t payload_len = grpc.ByteSizeLong();

    BreadcrumbEnvelope envelope;
    envelope.timestamp_ns = 1000;
    envelope.thread_id = 123;
    envelope.flow_id = 456;
    envelope.phase = BreadcrumbPhase::kFlowBegin;
    envelope.payload_type = PayloadType::kGrpcProto;
    envelope.payload_len = payload_len;

    uint32_t total_size = sizeof(BreadcrumbEnvelope) + payload_len;

    {
        auto log_or = RawCircularLog::CreateWriter(buffer.data(), buffer.size());
        ASSERT_TRUE(log_or.ok());
        auto log = std::move(*log_or);
        auto status = log->Push(total_size, [&](void* dest) {
            char* p = static_cast<char*>(dest);
            std::memcpy(p, &envelope, sizeof(BreadcrumbEnvelope));
            p += sizeof(BreadcrumbEnvelope);
            EXPECT_TRUE(grpc.SerializeToArray(p, payload_len));
        });
        ASSERT_TRUE(status.ok());
    }

    auto entries = BreadcrumbParser::Parse(buffer);
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].flow_id(), 456);
    EXPECT_EQ(entries[0].thread_id(), 123);
    EXPECT_EQ(entries[0].timestamp_ns(), 1000);
    ASSERT_TRUE(entries[0].has_grpc());
    EXPECT_EQ(entries[0].grpc().method_hash(), 0xABCDEF);
}

TEST(BreadcrumbMetadataTest, ResolvesMultipleServices) {
    // 1. Verify EmulatorController Resolution
    uint32_t key_hash = 3192300597;
    EXPECT_EQ(GetMethodName(key_hash), "sendKey");

    android::emulation::control::KeyboardEvent key_event;
    key_event.set_key("Escape");
    std::string payload = ResolvePayload(key_hash, key_event.SerializeAsString(),
                                         GrpcBreadcrumb::PRE_SEND_MESSAGE);
    EXPECT_NE(payload.find("Escape"), std::string::npos);

    // 2. Verify SensorService Resolution
    uint32_t sensor_hash = 1340809274;
    EXPECT_EQ(GetMethodName(sensor_hash), "setSensor");

    android::emulation::control::incubating::SensorValue sensor_val;
    sensor_val.set_target(
            android::emulation::control::incubating::SensorValue::SENSOR_SENSOR_TYPE_ACCELERATION);
    std::string s_payload = ResolvePayload(sensor_hash, sensor_val.SerializeAsString(),
                                           GrpcBreadcrumb::PRE_SEND_MESSAGE);
    EXPECT_NE(s_payload.find("ACCELERATION"), std::string::npos);

    // 3. Verify CarService Resolution
    uint32_t car_hash = 1094894787;
    EXPECT_EQ(GetMethodName(car_hash), "sendCarEvent");

    android::emulation::control::incubating::CarEvent car_event;
    car_event.set_data("test_car_data");
    std::string c_payload = ResolvePayload(car_hash, car_event.SerializeAsString(),
                                           GrpcBreadcrumb::PRE_SEND_MESSAGE);
    EXPECT_NE(c_payload.find("test_car_data"), std::string::npos);
}

}  // namespace android::crashreport::breadcrumbs
