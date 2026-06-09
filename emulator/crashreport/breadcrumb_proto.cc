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

namespace {

// Annotation buffer is capped at 20KB by the crash reporter. We use 16KB.
constexpr size_t kBreadcrumbBufferSize = 16384;

template <size_t Size>
struct BreadcrumbLogState {
    BinaryAnnotation<Size> annotation;
    std::unique_ptr<RawCircularLog> log;

    explicit BreadcrumbLogState(const char* name) : annotation(name) {
        auto log_res = RawCircularLog::CreateWriter(annotation.Data(), annotation.size());
        if (!log_res.ok()) {
            LOG(ERROR) << "Failed to initialize " << name
                       << " breadcrumb log: " << log_res.status();
        } else {
            log = std::move(*log_res);
        }
    }
};

}  // namespace

RawCircularLog* GetBreadcrumbLog(BreadcrumbType type) {
    switch (type) {
    case BreadcrumbType::kGrpc: {
        static BreadcrumbLogState<kBreadcrumbBufferSize> s_state("grpc_breadcrumbs");
        return s_state.log.get();
    }
    case BreadcrumbType::kAdb: {
        static BreadcrumbLogState<kBreadcrumbBufferSize> s_state("adb_breadcrumbs");
        return s_state.log.get();
    }

    default:
        LOG(ERROR) << "Unknown breadcrumb type";
        return nullptr;
    }
}

absl::Status LogBreadcrumb(BreadcrumbType type, uint64_t flow_id, BreadcrumbPhase phase,
                           PayloadType payload_type, std::string_view payload) {
    RawCircularLog* log = GetBreadcrumbLog(type);
    if (!log) {
        return absl::InternalError("Failed to get breadcrumb log");
    }

    uint16_t payload_len = static_cast<uint16_t>(payload.size());

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
        if (payload_len > 0) {
            std::memcpy(p, payload.data(), payload_len);
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
