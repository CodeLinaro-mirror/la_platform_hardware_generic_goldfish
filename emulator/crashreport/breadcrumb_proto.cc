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
#include "android/crashreport/breadcrumb_proto.h"

#include <atomic>
#include <cstring>

#include "absl/log/log.h"
#include "absl/time/clock.h"

#include "android/crashreport/binary_annotation.h"
#include "android/crashreport/thread.h"

namespace android::crashreport {

RawCircularLog* GetBreadcrumbLog(BreadcrumbType type) {
    switch (type) {
    case BreadcrumbType::kGrpc: {
        static BinaryAnnotation<16384> s_annotation("grpc_breadcrumbs");
        static const std::unique_ptr<RawCircularLog> kSLog = []() {
            auto log = RawCircularLog::CreateWriter(s_annotation.Data(), s_annotation.size());
            if (!log.ok()) {
                LOG(ERROR) << "Failed to initialize gRPC breadcrumb log: " << log.status();
                return std::unique_ptr<RawCircularLog>(nullptr);
            }
            return std::move(*log);
        }();
        return kSLog.get();
    }
    case BreadcrumbType::kAdb: {
        static BinaryAnnotation<16384> s_annotation("adb_breadcrumbs");
        static const std::unique_ptr<RawCircularLog> kSLog = []() {
            auto log = RawCircularLog::CreateWriter(s_annotation.Data(), s_annotation.size());
            if (!log.ok()) {
                LOG(ERROR) << "Failed to initialize ADB breadcrumb log: " << log.status();
                return std::unique_ptr<RawCircularLog>(nullptr);
            }
            return std::move(*log);
        }();
        return kSLog.get();
    }

    default:
        LOG(ERROR) << "Unknown breadcrumb type";
        return nullptr;
    }
}

absl::Status LogBreadcrumb(BreadcrumbType type, uint64_t flow_id, BreadcrumbPhase phase,
                           PayloadType payload_type, const void* payload_data,
                           uint16_t payload_len) {
    RawCircularLog* log = GetBreadcrumbLog(type);
    if (!log) {
        return absl::InternalError("Failed to get breadcrumb log");
    }

    BreadcrumbEnvelope envelope;
    envelope.timestamp_ns = absl::GetCurrentTimeNanos();
    envelope.thread_id = GetOsThreadId();
    envelope.flow_id = flow_id;
    envelope.phase = static_cast<uint8_t>(phase);
    envelope.payload_type = static_cast<uint8_t>(payload_type);
    envelope.payload_len = payload_len;

    uint32_t total_size = sizeof(BreadcrumbEnvelope) + payload_len;

    return log->Push(total_size, [&](void* dest) {
        char* p = static_cast<char*>(dest);
        std::memcpy(p, &envelope, sizeof(BreadcrumbEnvelope));
        p += sizeof(BreadcrumbEnvelope);
        if (payload_len > 0 && payload_data != nullptr) {
            std::memcpy(p, payload_data, payload_len);
        }
    });
}

uint64_t AllocateGlobalFlowId() {
    static std::atomic<uint64_t> sNextFlowId{1};
    // Relaxed increment is sufficient for uniqueness. We ignore overflow because at 10 billion
    // allocations per second, it would take approximately 58 years of continuous execution to
    // wrap around back to 0.
    return sNextFlowId.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace android::crashreport
