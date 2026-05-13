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
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_format.h"
#include "grpc_diagnostic.pb.h"

namespace android::crashreport::breadcrumbs {

using android::control::interceptor::GrpcBreadcrumb;

/**
 * @brief Represents a breadcrumb enriched with semantic metadata.
 *
 * While the raw GrpcBreadcrumb contains hashes and binary payloads, the
 * EnrichedBreadcrumb contains resolved names and human-readable strings.
 */
struct EnrichedBreadcrumb {
    GrpcBreadcrumb proto;          ///< The original raw breadcrumb proto.
    std::string_view method_name;  ///< Resolved gRPC method name (e.g., "sendKey").
    std::string resolved_payload;  ///< Deserialized and formatted payload content.
};

/**
 * @brief Represents the complete lifecycle of a single gRPC call.
 *
 * This structure aggregates all events related to a specific gRPC call ID,
 * tracking its duration, success status, and visual properties.
 * Data is stored by value to ensure memory safety across the rendering pipeline.
 */
struct CallLifecycle {
    uint32_t call_id = 0;                    ///< The unique gRPC call identifier.
    uint32_t color_slot = 0;                 ///< Assigned color index for visual differentiation.
    std::vector<EnrichedBreadcrumb> events;  ///< All breadcrumbs associated with this call.
    uint64_t start_ns = 0;   ///< Timestamp of the first event in the call (nanoseconds).
    uint64_t end_ns = 0;     ///< Timestamp of the last event in the call (nanoseconds).
    bool has_error = false;  ///< True if any event in the call reported a non-OK status.

    /**
     * @brief Custom stringifier for Abseil logging and formatting.
     */
    template <typename Sink>
    friend void AbslStringify(Sink& sink, const CallLifecycle& call) {
        absl::Format(&sink, "Call(id=%v, slot=%v, events=%zu, start=%v, end=%v, error=%v)",
                     call.call_id, call.color_slot, call.events.size(), call.start_ns, call.end_ns,
                     call.has_error);
    }
};

/**
 * @brief Represents a single thread's sequence of events.
 *
 * Each lane corresponds to one horizontal swimlane in the visual output,
 * typically representing an OS-level thread.
 */
struct TraceLane {
    uint64_t thread_id = 0;                  ///< The unique OS thread identifier.
    std::string thread_name;                 ///< Human-readable name of the thread, if available.
    bool is_crashing_thread = false;         ///< True if this thread was the site of the crash.
    std::vector<EnrichedBreadcrumb> events;  ///< Chronological sequence of events on this thread.

    /**
     * @brief Custom stringifier for Abseil logging and formatting.
     */
    template <typename Sink>
    friend void AbslStringify(Sink& sink, const TraceLane& lane) {
        absl::Format(&sink, "Lane(tid=%v, name=%s, crash=%v, events=%zu)", lane.thread_id,
                     lane.thread_name, lane.is_crashing_thread, lane.events.size());
    }
};

/**
 * @brief The unified model used by all renderers (ANSI, SVG).
 *
 * This is the final structured representation of the forensic data extracted
 * from the breadcrumb buffer, organized for multi-lane visualization.
 */
struct DiagnosticTrace {
    uint64_t global_start_ns = 0;     ///< Earliest timestamp in the entire trace.
    uint64_t global_end_ns = 0;       ///< Latest timestamp in the entire trace.
    uint64_t crashing_thread_id = 0;  ///< The ID of the thread that triggered the exception.

    /**
     * @brief Lanes ordered for display (typically crashing thread first).
     */
    std::vector<TraceLane> lanes;

    /**
     * @brief Call lifecycles grouped by ID for easy lookup and relation mapping.
     */
    absl::flat_hash_map<uint32_t, CallLifecycle> calls;

    /**
     * @brief Custom stringifier for Abseil logging and formatting.
     */
    template <typename Sink>
    friend void AbslStringify(Sink& sink, const DiagnosticTrace& trace) {
        absl::Format(&sink, "Trace(start=%v, end=%v, crash_tid=%v, lanes=%zu, calls=%zu)",
                     trace.global_start_ns, trace.global_end_ns, trace.crashing_thread_id,
                     trace.lanes.size(), trace.calls.size());
    }
};

}  // namespace android::crashreport::breadcrumbs
