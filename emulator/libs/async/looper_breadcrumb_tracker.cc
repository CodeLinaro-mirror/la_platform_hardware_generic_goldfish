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
#include "goldfish/async/looper_breadcrumb_tracker.h"

#include <algorithm>
#include <cstring>

#include "absl/log/log.h"
#include "absl/time/clock.h"

#include "android/crashreport/thread.h"

namespace goldfish::async {

using android::crashreport::AllocateGlobalFlowId;
using android::crashreport::BreadcrumbEnvelope;
using android::crashreport::RawLooperExecPayload;
using android::crashreport::RawLooperPostWithContextPayload;
using android::crashreport::RawLooperPostWithContextPayloadT;
using android::crashreport::RegisterLooper;
using goldfish::proto_data_store::RawCircularLog;

// Maximum length of the diagnostic context string copied to looper breadcrumbs.
// Capped to prevent long context strings from flooding the circular log.
constexpr size_t kMaxContextLen = 32;

static std::atomic<uint8_t> sNextLoopId{1};

LooperBreadcrumbTracker::LooperBreadcrumbTracker(std::string name)
        : name_(std::move(name)), loop_id_(sNextLoopId++), annotation_(name_) {
    // The looper local log (breadcrumb log) stores a circular buffer of recent event loop
    // posts and executions. This is written to a DynamicBinaryAnnotation, which is captured
    // by Crashpad during minidump generation. If initialization fails, the event loop will
    // still function normally, but crash diagnostics will not have event execution history.
    if (auto log = RawCircularLog::CreateWriter(annotation_.Data().data(), kCircularLogSize);
        !log.ok()) {
        LOG(ERROR) << "Failed to initialize looper local breadcrumb log (crash dumps will lack "
                      "loop history): "
                   << log.status();
    } else {
        breadcrumb_log_ = std::move(*log);
    }
    RegisterLooper(loop_id_, name_);
}

FlowId LooperBreadcrumbTracker::LogPost(const PostOptions& options) {
    FlowId flow_id = AllocateGlobalFlowId();
    uint8_t string_len = std::min(options.context.size(), kMaxContextLen);

    RawLooperPostWithContextPayloadT<kMaxContextLen> post_payload;
    post_payload.caller_pc = options.caller_pc;
    post_payload.loop_id = loop_id_;
    post_payload.context_len = string_len;
    if (string_len > 0) {
        std::memcpy(post_payload.context_data, options.context.data(), string_len);
    }

    // Only write the active part of the struct containing the actual context string to the log.
    size_t struct_size = sizeof(RawLooperPostWithContextPayload) + string_len;
    LogEvent(flow_id, BreadcrumbPhase::kFlowBegin, PayloadType::kLooperPostContextRaw,
             std::string_view(reinterpret_cast<const char*>(&post_payload), struct_size))
            .IgnoreError();

    return flow_id;
}

void LooperBreadcrumbTracker::LogExecute(FlowId flow_id) {
    RawLooperExecPayload exec_payload{
        .loop_id = loop_id_,
    };
    LogEvent(flow_id, BreadcrumbPhase::kFlowEnd, PayloadType::kLooperExecRaw,
             std::string_view(reinterpret_cast<const char*>(&exec_payload), sizeof(exec_payload)))
            .IgnoreError();
}

absl::Status LooperBreadcrumbTracker::LogEvent(FlowId flow_id, BreadcrumbPhase phase,
                                               PayloadType payload_type, std::string_view payload) {
    if (!breadcrumb_log_) {
        return absl::InternalError("Breadcrumb log not initialized");
    }
    return android::crashreport::LogBreadcrumbTo(breadcrumb_log_.get(), flow_id, phase,
                                                 payload_type, payload);
}

}  // namespace goldfish::async
