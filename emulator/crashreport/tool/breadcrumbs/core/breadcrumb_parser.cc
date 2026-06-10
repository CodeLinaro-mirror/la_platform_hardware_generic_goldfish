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

#include <bit>
#include <string_view>

#include "emulator/crashreport/include/android/crashreport/breadcrumb_proto.h"
#include "goldfish/raw_circular_log.h"

namespace android::crashreport::breadcrumbs {

static_assert(std::endian::native == std::endian::little,
              "Only little-endian systems are supported");

using android::control::breadcrumbs::AdbPayload;
using android::control::breadcrumbs::Breadcrumb;
using android::control::breadcrumbs::GrpcPayload;
using android::control::breadcrumbs::LooperPayload;
using android::crashreport::BreadcrumbEnvelope;
using android::crashreport::BreadcrumbPhase;
using android::crashreport::PayloadType;
using android::crashreport::RawAdbPayload;
using android::crashreport::RawLooperExecPayload;
using android::crashreport::RawLooperPostWithContextPayload;
using goldfish::proto_data_store::RawCircularLog;

namespace {

/**
 * @brief Parses the custom payload portion of the breadcrumb envelope and populates the event.
 *
 * Extracts the payload based on the payload type (e.g. gRPC or ADB logs), and attaches the parsed
 * payload metadata to the event.
 *
 * @param envelope The standard breadcrumb envelope.
 * @param payload The raw payload data slice as a string_view.
 * @param event Output parameter populated with the parsed payload.
 */
void ParsePayload(const BreadcrumbEnvelope& envelope, std::string_view payload, Breadcrumb& event) {
    switch (envelope.payload_type) {
    case PayloadType::kGrpcProto: {
        GrpcPayload grpc;
        if (grpc.ParseFromArray(payload.data(), payload.size())) {
            *event.mutable_grpc() = grpc;
        }
        break;
    }
    case PayloadType::kAdbProto: {
        AdbPayload adb;
        if (adb.ParseFromArray(payload.data(), payload.size())) {
            *event.mutable_adb() = adb;
        }
        break;
    }
    case PayloadType::kAdbRawToGuest:
    case PayloadType::kAdbRawToHost: {
        if (payload.size() >= sizeof(RawAdbPayload)) {
            RawAdbPayload raw_adb;
            std::memcpy(&raw_adb, payload.data(), sizeof(RawAdbPayload));
            AdbPayload adb;
            adb.set_command(raw_adb.command);
            adb.set_direction(envelope.payload_type == PayloadType::kAdbRawToGuest
                                      ? AdbPayload::TO_GUEST
                                      : AdbPayload::TO_HOST);

            uint8_t snippet_len = raw_adb.snippet_len;
            if (snippet_len > 0 && sizeof(RawAdbPayload) + snippet_len <= payload.size()) {
                adb.set_data_snippet(payload.data() + sizeof(RawAdbPayload), snippet_len);
            }
            *event.mutable_adb() = adb;
        }
        break;
    }
    case PayloadType::kLooperPostContextRaw: {
        if (payload.size() >= sizeof(RawLooperPostWithContextPayload)) {
            RawLooperPostWithContextPayload raw_post;
            std::memcpy(&raw_post, payload.data(), sizeof(RawLooperPostWithContextPayload));
            LooperPayload looper;
            looper.set_event(LooperPayload::POST);
            looper.set_caller_pc(raw_post.caller_pc);
            looper.set_loop_id(raw_post.loop_id);

            size_t string_len = std::min(
                    static_cast<size_t>(raw_post.context_len),
                    static_cast<size_t>(payload.size() - sizeof(RawLooperPostWithContextPayload)));
            if (string_len > 0) {
                looper.set_context(std::string(
                        payload.data() + sizeof(RawLooperPostWithContextPayload), string_len));
            }
            *event.mutable_looper() = looper;
        }
        break;
    }
    case PayloadType::kLooperExecRaw: {
        if (payload.size() >= sizeof(RawLooperExecPayload)) {
            RawLooperExecPayload raw_exec;
            std::memcpy(&raw_exec, payload.data(), sizeof(RawLooperExecPayload));
            LooperPayload looper;
            looper.set_event(LooperPayload::EXECUTE);
            looper.set_loop_id(raw_exec.loop_id);
            *event.mutable_looper() = looper;
        }
        break;
    }
    default:
        break;
    }
}

}  // namespace

std::vector<android::control::breadcrumbs::Breadcrumb> BreadcrumbParser::Parse(
        const std::vector<uint8_t>& buffer) {
    if (buffer.size() <= RawCircularLog::kHeaderSize) return {};

    auto log = RawCircularLog::CreateReader(const_cast<uint8_t*>(buffer.data()), buffer.size());
    if (!log.ok()) {
        return {};
    }

    std::vector<Breadcrumb> entries;
    entries.reserve((*log)->ObjectCount());

    (*log)->ForEach([&](void* data, uint16_t size) {
        if (size < sizeof(BreadcrumbEnvelope)) {
            return true;  // Skip invalid records
        }

        BreadcrumbEnvelope envelope;
        std::memcpy(&envelope, data, sizeof(BreadcrumbEnvelope));

        Breadcrumb event;
        event.set_timestamp_ns(envelope.timestamp_ns);
        event.set_thread_id(envelope.thread_id);
        event.set_flow_id(envelope.flow_id);

        switch (envelope.phase) {
        case BreadcrumbPhase::kFlowBegin:
            event.set_phase(Breadcrumb::FLOW_BEGIN);
            break;
        case BreadcrumbPhase::kFlowStep:
            event.set_phase(Breadcrumb::FLOW_STEP);
            break;
        case BreadcrumbPhase::kFlowEnd:
            event.set_phase(Breadcrumb::FLOW_END);
            break;
        default:
            event.set_phase(Breadcrumb::INSTANT);
            break;
        }

        const char* payload_ptr = static_cast<const char*>(data) + sizeof(BreadcrumbEnvelope);
        const uint16_t payload_len = envelope.payload_len;

        if (payload_len > 0 && payload_len <= size - sizeof(BreadcrumbEnvelope)) {
            ParsePayload(envelope, std::string_view(payload_ptr, payload_len), event);
        }

        entries.push_back(event);
        return true;
    });

    return entries;
}

}  // namespace android::crashreport::breadcrumbs
