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
#include "android/crashreport/breadcrumbs/mermaid_renderer.h"

#include <algorithm>
#include <sstream>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/time/time.h"

#include "android/crashreport/breadcrumbs/util.h"

namespace android::crashreport::breadcrumbs {

namespace {

/**
 * @brief Escapes strings for use in Mermaid labels.
 */
std::string EscapeMermaid(std::string_view text) {
    return absl::StrReplaceAll(text,
                               {{"\"", "'"}, {"\n", " "}, {"\r", ""}, {"{", "("}, {"}", ")"}});
}

std::string PhaseToLabel(android::control::breadcrumbs::GrpcPayload::GrpcPhase phase) {
    using android::control::breadcrumbs::GrpcPayload;
    switch (phase) {
    case GrpcPayload::START:
        return "START";
    case GrpcPayload::PRE_SEND_INITIAL_METADATA:
        return "SEND_META";
    case GrpcPayload::PRE_SEND_MESSAGE:
        return "SEND_MSG";
    case GrpcPayload::PRE_SEND_STATUS:
        return "SEND_STAT";
    case GrpcPayload::PRE_RECV_INITIAL_METADATA:
        return "RECV_META";
    case GrpcPayload::PRE_RECV_MESSAGE:
        return "RECV_MSG";
    case GrpcPayload::PRE_RECV_STATUS:
        return "RECV_STAT";
    case GrpcPayload::END_OF_CALL:
        return "END";
    default:
        return "UNKNOWN";
    }
}

}  // namespace

std::string MermaidRenderer::Render(const DiagnosticTrace& trace) const {
    std::stringstream ss;

    // Initialize the sequence diagram
    ss << "sequenceDiagram\n";
    ss << "    autonumber\n";  // Optional: adds sequential numbers to arrows

    // 1. Participants (Threads)
    absl::flat_hash_map<uint64_t, std::string> tid_to_alias;
    for (size_t i = 0; i < trace.lanes.size(); ++i) {
        const auto& lane = trace.lanes[i];
        const std::string alias = "T" + std::to_string(i);
        tid_to_alias[lane.thread_id] = alias;

        std::string name = lane.thread_name.empty() ? absl::StrFormat("Thread %d", lane.thread_id)
                                                    : lane.thread_name;
        if (lane.is_crashing_thread) {
            name += " [*]";
        }

        ss << "    participant " << alias << " as " << name << "\n";
    }

    // 2. Collect and sort all events globally
    struct GlobalEvent {
        const EnrichedBreadcrumb* breadcrumb;
        uint64_t tid;
        bool is_crash_thread;
    };

    std::vector<GlobalEvent> timeline;
    for (const auto& lane : trace.lanes) {
        for (const auto& event : lane.events) {
            timeline.push_back({&event, lane.thread_id, lane.is_crashing_thread});
        }
    }

    std::ranges::sort(timeline, [](const GlobalEvent& a, const GlobalEvent& b) {
        return a.breadcrumb->proto.timestamp_ns() < b.breadcrumb->proto.timestamp_ns();
    });

    // Determine the global start time to calculate relative offsets
    const uint64_t start_time_ns =
            timeline.empty() ? 0 : timeline.front().breadcrumb->proto.timestamp_ns();

    // 3. Render Events
    absl::flat_hash_map<uint32_t, uint64_t> call_to_last_tid;

    for (const auto& ge : timeline) {
        const auto& b = *ge.breadcrumb;
        const auto& proto = b.proto;
        const uint64_t cid = proto.flow_id();
        const uint64_t tid = ge.tid;
        const std::string target = tid_to_alias[tid];

        // Format relative and absolute time (e.g., 16:57:18.123456 (+1.2ms))
        const uint64_t ts_ns = proto.timestamp_ns();
        const std::string time_str = FormatEventTime(ts_ns, start_time_ns);

        // Format base label
        std::string label_phase = "UNKNOWN";
        if (proto.has_grpc()) {
            label_phase = PhaseToLabel(proto.grpc().grpc_phase());
        } else if (proto.has_adb()) {
            label_phase = "ADB";
        } else if (proto.has_looper()) {
            switch (proto.looper().event()) {
            case android::control::breadcrumbs::LooperPayload::POST:
                label_phase = "POST";
                break;
            case android::control::breadcrumbs::LooperPayload::EXECUTE:
                label_phase = "EXEC";
                break;
            default:
                label_phase = "LOOPER";
                break;
            }
        }

        const std::string label = absl::StrFormat("%s | [%v] %s: %s", time_str, cid, label_phase,
                                                  EscapeMermaid(b.method_name));

        const bool is_new_call = (call_to_last_tid.find(cid) == call_to_last_tid.end());
        const std::string source = is_new_call ? target : tid_to_alias[call_to_last_tid.at(cid)];

        // Layout routing: Notes for same-thread, Arrows for cross-thread
        if (source == target) {
            ss << "    Note over " << target << ": " << label << "\n";
        } else {
            bool is_end = false;
            if (proto.has_grpc()) {
                is_end = (proto.grpc().grpc_phase() ==
                          android::control::breadcrumbs::GrpcPayload::END_OF_CALL);
            } else if (proto.has_adb() || proto.has_looper()) {
                is_end = (proto.phase() == Breadcrumb::FLOW_END);
            }
            const std::string arrow = is_end ? "-->>" : "->>";
            ss << "    " << source << arrow << target << ": " << label << "\n";
        }

        // Add payload as a sub-note
        if (!b.resolved_payload.empty()) {
            ss << "    Note over " << target << ": " << EscapeMermaid(b.resolved_payload) << "\n";
        }

        // Add explicit error note if a gRPC call fails
        if (proto.has_grpc() &&
            proto.grpc().status_code() != android::control::breadcrumbs::GrpcPayload::OK) {
            ss << "    Note over " << target
               << ": ERROR: " << static_cast<int>(proto.grpc().status_code()) << "\n";
        }

        call_to_last_tid[cid] = tid;
    }

    // 4. Demarcate Crash Site with a highlighted bounding box
    if (!timeline.empty()) {
        const auto& last = timeline.back();
        if (last.is_crash_thread && last.breadcrumb->proto.has_grpc() &&
            last.breadcrumb->proto.grpc().status_code() !=
                    android::control::breadcrumbs::GrpcPayload::OK) {
            // Draws a red-tinted rectangle behind the fatal exception note
            ss << "    rect rgb(255, 200, 200)\n";
            ss << "    Note right of " << tid_to_alias[last.tid] << ": 💥 FATAL EXCEPTION\n";
            ss << "    end\n";
        }
    }

    return ss.str();
}

}  // namespace android::crashreport::breadcrumbs