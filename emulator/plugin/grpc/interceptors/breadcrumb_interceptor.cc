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
#include "android/control/interceptor/breadcrumb_interceptor.h"

#include <zlib.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/message.h"
#include "grpc_diagnostic.pb.h"

#include "android/base/clock.h"
#include "android/crashreport/binary_annotation.h"
#include "android/crashreport/thread.h"
#include "goldfish/circular_message_log.h"

namespace android::control::interceptor {

using ::android::control::interceptor::GrpcBreadcrumb;
using android::crashreport::BinaryAnnotation;
using android::crashreport::GetOsThreadId;
using goldfish::proto_data_store::ProtoCircularLog;
using grpc::experimental::ClientRpcInfo;
using grpc::experimental::InterceptionHookPoints;
using grpc::experimental::InterceptorBatchMethods;
using grpc::experimental::ServerRpcInfo;

namespace {

/**
 * @brief Maximum size of a gRPC payload to capture in the circular buffer.
 *
 * We use 256 bytes as a threshold to selectively capture high-signal
 * control messages while filtering out heavy data streams.
 *
 * MESSAGE SIZES:
 * - High-Signal (<100B): MouseEvent, KeyboardEvent, GpsState, BatteryState.
 *   These are discrete user actions/state changes and are fully captured.
 * - Contextual (100B-1KB): TouchEvent (multi-finger), EmulatorStatus.
 *   These are truncated or skipped to prevent buffer exhaustion.
 * - Heavy Data (>1KB): Image (pixels), AudioPacket, LogMessage, ClipData.
 *   These are never captured as they would wrap the 8KB buffer instantly.
 */
constexpr uint32_t kMaxCapturedPayloadSize = 256;

ProtoCircularLog<GrpcBreadcrumb>* GetLog() {
    // 16KB Crashpad Annotation that will be captured in minidumps.
    // Note: Crashpad has an internal limit (kValueMaxSize) of ~20KB per annotation.
    // 16KB is a safe power-of-2 that ensures sufficient forensic depth (~3-5 seconds)
    // even during high-frequency interaction bursts.
    static BinaryAnnotation<16384> s_grpc_annotation("grpc_breadcrumbs");
    static const std::unique_ptr<ProtoCircularLog<GrpcBreadcrumb>> kSLog = []() {
        auto log = ProtoCircularLog<GrpcBreadcrumb>::CreateWriter(s_grpc_annotation.Data(),
                                                                  s_grpc_annotation.size());
        if (!log.ok()) {
            LOG(ERROR) << "Failed to initialize gRPC breadcrumb log: " << log.status();
            return std::unique_ptr<ProtoCircularLog<GrpcBreadcrumb>>(nullptr);
        }
        return std::move(*log);
    }();
    return kSLog.get();
}

uint32_t GetMethodCrc(const char* method) {
    if (!method) return 0;
    return static_cast<uint32_t>(
            crc32(0, reinterpret_cast<const Bytef*>(method), static_cast<uInt>(strlen(method))));
}

uint32_t NextCallId() {
    // Atomically incrementing call ID to ensure uniqueness across threads, note we don't need
    // strict ordering no need to synchronize all the memory, just this one.
    static std::atomic<uint32_t> s_call_id{1};
    return s_call_id.fetch_add(1, std::memory_order_relaxed);
}

uint64_t GetTimestampNs() {
    return static_cast<uint64_t>(absl::ToUnixNanos(absl::Now()));
}

void LogEvent(const GrpcBreadcrumb& event) {
    if (auto* log = GetLog()) {
        (void)log->Push(event);
    }
}

void SetMessageDetail(GrpcBreadcrumb& event, const ::google::protobuf::Message* msg) {
    if (!msg) return;
    auto size = static_cast<uint32_t>(msg->ByteSizeLong());
    if (size <= kMaxCapturedPayloadSize) {
        event.set_payload(msg->SerializeAsString());
    } else {
        event.set_msg_size(size);
    }
}

}  // namespace

ProtoCircularLog<GrpcBreadcrumb>* BreadcrumbInterceptor::GetLogForTesting() {
    return GetLog();
}

BreadcrumbInterceptor::BreadcrumbInterceptor(const ClientRpcInfo* info)
        : call_id_(NextCallId()), method_hash_(info ? GetMethodCrc(info->method()) : 0) {
    GrpcBreadcrumb event;
    event.set_call_id(call_id_);
    event.set_method_hash(method_hash_);
    event.set_phase(GrpcBreadcrumb::START);
    event.set_timestamp_ns(GetTimestampNs());
    event.set_thread_id(GetOsThreadId());
    LogEvent(event);
}

BreadcrumbInterceptor::BreadcrumbInterceptor(const ServerRpcInfo* info)
        : call_id_(NextCallId()), method_hash_(info ? GetMethodCrc(info->method()) : 0) {
    GrpcBreadcrumb event;
    event.set_call_id(call_id_);
    event.set_method_hash(method_hash_);
    event.set_phase(GrpcBreadcrumb::START);
    event.set_timestamp_ns(GetTimestampNs());
    event.set_thread_id(GetOsThreadId());
    LogEvent(event);
}

BreadcrumbInterceptor::~BreadcrumbInterceptor() {
    GrpcBreadcrumb event;
    event.set_call_id(call_id_);
    event.set_method_hash(method_hash_);
    event.set_phase(GrpcBreadcrumb::END_OF_CALL);
    event.set_timestamp_ns(GetTimestampNs());
    event.set_thread_id(GetOsThreadId());
    LogEvent(event);
}

void BreadcrumbInterceptor::Intercept(InterceptorBatchMethods* methods) {
    // We iterate over the hooks we care about. This handles gRPC batching
    // correctly by allowing multiple events to be logged per Intercept() call.
    static constexpr InterceptionHookPoints kInterestingHooks[] = {
        InterceptionHookPoints::PRE_SEND_INITIAL_METADATA, InterceptionHookPoints::PRE_SEND_MESSAGE,
        InterceptionHookPoints::POST_RECV_MESSAGE,         InterceptionHookPoints::PRE_RECV_MESSAGE,
        InterceptionHookPoints::PRE_SEND_STATUS,           InterceptionHookPoints::PRE_RECV_STATUS,
    };

    GrpcBreadcrumb event;
    event.set_call_id(call_id_);
    event.set_method_hash(method_hash_);
    event.set_timestamp_ns(GetTimestampNs());
    event.set_thread_id(GetOsThreadId());

    for (auto hook : kInterestingHooks) {
        if (!methods->QueryInterceptionHookPoint(hook)) {
            continue;
        }

        // Clear out fields that may not be set for every event, they will be overwritten if the
        // hook provides them.
        event.clear_detail();

        switch (hook) {
        case InterceptionHookPoints::PRE_SEND_INITIAL_METADATA:
            event.set_phase(GrpcBreadcrumb::PRE_SEND_INITIAL_METADATA);
            break;
        case InterceptionHookPoints::PRE_SEND_MESSAGE:
            event.set_phase(GrpcBreadcrumb::PRE_SEND_MESSAGE);
            SetMessageDetail(event, static_cast<const ::google::protobuf::Message*>(
                                            methods->GetSendMessage()));
            break;
        case InterceptionHookPoints::POST_RECV_MESSAGE:
            event.set_phase(GrpcBreadcrumb::POST_RECV_MESSAGE);
            SetMessageDetail(event, static_cast<const ::google::protobuf::Message*>(
                                            methods->GetRecvMessage()));
            break;
        case InterceptionHookPoints::PRE_RECV_MESSAGE:
            event.set_phase(GrpcBreadcrumb::PRE_RECV_MESSAGE);
            break;
        case InterceptionHookPoints::PRE_SEND_STATUS:
            event.set_phase(GrpcBreadcrumb::PRE_SEND_STATUS);
            event.set_status_code(static_cast<GrpcBreadcrumb::GrpcStatusCode>(
                    methods->GetSendStatus().error_code()));
            break;
        case InterceptionHookPoints::PRE_RECV_STATUS:
            event.set_phase(GrpcBreadcrumb::PRE_RECV_STATUS);
            event.set_status_code(static_cast<GrpcBreadcrumb::GrpcStatusCode>(
                    methods->GetRecvStatus()->error_code()));
            break;
        default:
            DCHECK(false) << "Unexpected hook point: " << static_cast<int>(hook);
            continue;
        }
        LogEvent(event);
    }

    methods->Proceed();
}

BreadcrumbInterceptorFactory::BreadcrumbInterceptorFactory() = default;

grpc::experimental::Interceptor* BreadcrumbInterceptorFactory::CreateServerInterceptor(
        ServerRpcInfo* info) {
    // See https://grpc.io/docs/guides/interceptors for details on how interceptors
    // are managed. The gRPC engine will take ownership of this pointer.
    return new BreadcrumbInterceptor(info);
}

grpc::experimental::Interceptor* BreadcrumbInterceptorFactory::CreateClientInterceptor(
        ClientRpcInfo* info) {
    // See https://grpc.io/docs/guides/interceptors for details on how interceptors
    // are managed. The gRPC engine will take ownership of this pointer.
    return new BreadcrumbInterceptor(info);
}

}  // namespace android::control::interceptor
