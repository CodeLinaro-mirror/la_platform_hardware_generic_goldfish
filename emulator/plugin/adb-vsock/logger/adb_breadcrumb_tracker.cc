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
#include "goldfish/adb/adb_breadcrumb_tracker.h"

#include <algorithm>
#include <cstring>

#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/time/clock.h"

#include "android/crashreport/binary_annotation.h"
#include "android/crashreport/thread.h"
#include "goldfish/circular_message_log.h"

namespace goldfish::adb {

using android::control::breadcrumbs::Breadcrumb;
using android::crashreport::BinaryAnnotation;
using android::crashreport::GetOsThreadId;
using goldfish::proto_data_store::ProtoCircularLog;

namespace {

constexpr size_t kMaxPendingOpens = 16;
constexpr size_t kMaxOpenStreams = 31;
constexpr size_t kMaxPayloadSnippetSize = 32;

uint64_t CombineU32(uint32_t high, uint32_t low) {
    return (static_cast<uint64_t>(high) << 32) | low;
}

uint64_t GetLookupFlowId(const AMessage& message, bool to_guest) {
    return to_guest ? CombineU32(message.arg0, message.arg1)
                    : CombineU32(message.arg1, message.arg0);
}

ProtoCircularLog<Breadcrumb>* GetLog() {
    // 20479 is the maximum value size allowed by Crashpad for an annotation.
    static BinaryAnnotation<20479> s_adb_annotation("adb_breadcrumbs");
    static const std::unique_ptr<ProtoCircularLog<Breadcrumb>> kSLog = []() {
        auto log = ProtoCircularLog<Breadcrumb>::CreateWriter(s_adb_annotation.Data(),
                                                              s_adb_annotation.size());
        if (!log.ok()) {
            LOG(ERROR) << "Failed to create Breadcrumb writer: " << log.status().message();
            return std::unique_ptr<ProtoCircularLog<Breadcrumb>>(nullptr);
        }
        return std::move(*log);
    }();
    return kSLog.get();
}

}  // namespace

void AdbBreadcrumbTracker::OnPacket(const AMessage& message, const char* data, bool to_guest) {
    auto* log = GetLog();
    if (!log) return;

    Breadcrumb event;
    const uint64_t flow_id = CombineU32(message.arg0, message.arg1);
    event.set_flow_id(flow_id);
    event.set_timestamp_ns(static_cast<uint64_t>(absl::ToUnixNanos(absl::Now())));
    const uint64_t t_tid = GetOsThreadId();
    event.set_thread_id(t_tid);

    // Set Phase
    switch (message.command) {
    case kAdbOpen:
        event.set_phase(Breadcrumb::FLOW_BEGIN);
        break;
    case kAdbClse:
        event.set_phase(Breadcrumb::FLOW_END);
        break;
    case kAdbWrte:
    case kAdbOkay:
        event.set_phase(Breadcrumb::FLOW_STEP);
        break;
    default:
        event.set_phase(Breadcrumb::INSTANT);
        break;
    }

    auto* adb_payload = event.mutable_adb();
    adb_payload->set_command(message.command);
    adb_payload->set_direction(to_guest ? android::control::breadcrumbs::AdbPayload::TO_GUEST
                                        : android::control::breadcrumbs::AdbPayload::TO_HOST);

    bool is_sync_stream = false;
    {
        absl::MutexLock lock(&open_streams_mutex_);

        // Handle state updates
        switch (message.command) {
        case kAdbOpen:
            HandleOpen(message, data, to_guest);
            break;
        case kAdbOkay:
            HandleOkay(message, to_guest);
            break;
        case kAdbClse:
            HandleClose(message, to_guest);
            break;
        }

        const uint64_t lookup_flow_id = GetLookupFlowId(message, to_guest);
        is_sync_stream = IsSyncStream(lookup_flow_id);
    }

    const size_t snippet_len = std::min<size_t>(message.data_length, kMaxPayloadSnippetSize);

    if (message.command == kAdbWrte && is_sync_stream && data != nullptr &&
        message.data_length >= 4) {
        adb_payload->set_data_snippet(data, 4);
    } else if (capture_snippets_ && snippet_len > 0 && data != nullptr) {
        adb_payload->set_data_snippet(data, snippet_len);
    }

    log->Push(event).IgnoreError();
}

void AdbBreadcrumbTracker::HandleOpen(const AMessage& message, const char* data, bool to_guest) {
    if (data == nullptr) return;
    const bool is_sync = absl::StartsWith(
            std::string_view(data, std::min<size_t>(message.data_length, 64)), "sync:");
    const uint64_t key = CombineU32(to_guest, message.arg0);
    if (pending_opens_.size() >= kMaxPendingOpens) {
        if (!pending_opens_order_.empty()) {
            const uint64_t oldest_key = pending_opens_order_.front();
            pending_opens_.erase(oldest_key);
            pending_opens_order_.erase(pending_opens_order_.begin());
        }
    }
    pending_opens_[key] = is_sync;
    pending_opens_order_.push_back(key);
}

void AdbBreadcrumbTracker::HandleOkay(const AMessage& message, bool to_guest) {
    const uint64_t key = CombineU32(!to_guest, message.arg1);
    auto it = pending_opens_.find(key);
    if (it == pending_opens_.end()) return;

    const bool is_sync = it->second;
    pending_opens_.erase(it);

    auto it_order = std::find(pending_opens_order_.begin(), pending_opens_order_.end(), key);
    if (it_order != pending_opens_order_.end()) {
        pending_opens_order_.erase(it_order);
    }

    const uint64_t flow = GetLookupFlowId(message, to_guest);

    while (open_streams_.size() >= kMaxOpenStreams) {
        open_streams_.erase(open_streams_.begin());
    }
    open_streams_.push_back({flow, is_sync});
}

void AdbBreadcrumbTracker::HandleClose(const AMessage& message, bool to_guest) {
    const uint64_t flow = GetLookupFlowId(message, to_guest);

    auto it = std::find_if(open_streams_.begin(), open_streams_.end(),
                           [&](const auto& p) { return p.first == flow; });
    if (it != open_streams_.end()) {
        open_streams_.erase(it);
    }

    // Remove from pending opens
    const uint64_t key_self = CombineU32(to_guest, message.arg0);
    const uint64_t key_other = CombineU32(!to_guest, message.arg1);

    pending_opens_.erase(key_self);
    auto it_order = std::find(pending_opens_order_.begin(), pending_opens_order_.end(), key_self);
    if (it_order != pending_opens_order_.end()) {
        pending_opens_order_.erase(it_order);
    }

    pending_opens_.erase(key_other);
    it_order = std::find(pending_opens_order_.begin(), pending_opens_order_.end(), key_other);
    if (it_order != pending_opens_order_.end()) {
        pending_opens_order_.erase(it_order);
    }
}

bool AdbBreadcrumbTracker::IsSyncStream(uint64_t flow_id) {
    // Note tnis is a very short vector (32 max) so linear search is fine
    auto it = std::find_if(open_streams_.begin(), open_streams_.end(),
                           [&](const auto& p) { return p.first == flow_id; });
    return it != open_streams_.end() && it->second;
}

ProtoCircularLog<android::control::breadcrumbs::Breadcrumb>*
AdbBreadcrumbTracker::GetLogForTesting() {
    return GetLog();
}

void AdbBreadcrumbTracker::OnOutOfSync(const std::string& reason) {
    Breadcrumb event;
    event.set_timestamp_ns(static_cast<uint64_t>(absl::ToUnixNanos(absl::Now())));
    uint64_t t_tid = GetOsThreadId();
    event.set_thread_id(t_tid);
    event.set_phase(Breadcrumb::INSTANT);

    auto* adb_payload = event.mutable_adb();
    adb_payload->set_command(0);
    adb_payload->set_data_snippet(reason);

    if (auto* log = GetLog()) {
        log->Push(event).IgnoreError();
    }
}

}  // namespace goldfish::adb
