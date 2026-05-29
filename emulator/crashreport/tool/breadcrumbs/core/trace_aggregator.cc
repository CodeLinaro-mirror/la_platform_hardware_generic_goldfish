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
#include "android/crashreport/breadcrumbs/trace_aggregator.h"

#include <algorithm>
#include <set>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/time/time.h"

#include "goldfish/unique_id_allocator.h"

namespace android::crashreport::breadcrumbs {

/**
 * @brief Represents a temporal boundary (start or end) of a gRPC call.
 *
 * This structure is used as an event in the sweep-line algorithm within
 * AssignColorSlots to track the number of concurrent active calls at any
 * given point in time.
 */
struct Boundary {
    uint64_t ts;       ///< Nanosecond timestamp of the boundary.
    uint64_t call_id;  ///< The call this boundary belongs to.
    bool is_start;     ///< True if this is the start of the call, false if end.

    /**
     * @brief Sorts boundaries chronologically for the sweep-line algorithm.
     *
     * The sorting logic is critical for maximizing color recycling:
     * 1. Primary sort is by timestamp (ts).
     * 2. If timestamps are identical, 'End' events are processed before 'Start'
     *    events. This ensures that a color slot released at time T can be
     *    immediately reused by another call starting at time T.
     *
     * @param other The other boundary to compare against.
     * @return True if this boundary should be processed before 'other'.
     */
    bool operator<(const Boundary& other) const {
        if (ts != other.ts) return ts < other.ts;
        // Process End events (is_start=false) before Start events (is_start=true)
        // Note: false < true.
        return is_start < other.is_start;
    }

    template <typename Sink>
    friend void AbslStringify(Sink& sink, const Boundary& b) {
        absl::Format(&sink, "Boundary(ts=%v, call_id=%v, type=%s)", b.ts, b.call_id,
                     b.is_start ? "START" : "END");
    }
};

DiagnosticTrace TraceAggregator::Aggregate(const std::vector<EnrichedBreadcrumb>& events,
                                           uint64_t crashing_thread_id) {
    DiagnosticTrace trace;
    trace.crashing_thread_id = crashing_thread_id;

    if (events.empty()) {
        VLOG(1) << "TraceAggregator: No events to aggregate.";
        return trace;
    }

    trace.global_start_ns = events.front().proto.timestamp_ns();
    trace.global_end_ns = events.back().proto.timestamp_ns();
    const uint64_t duration_ns = trace.global_end_ns - trace.global_start_ns;

    LOG(INFO) << "TraceAggregator: Aggregating " << events.size() << " events over "
              << absl::Nanoseconds(duration_ns) << " (crashing thread: " << crashing_thread_id
              << ")";

    // Pass 1: Discover unique threads and group by call_id.
    std::set<uint64_t> thread_ids;
    for (const auto& e : events) {
        thread_ids.insert(e.proto.thread_id());

        auto& call = trace.calls[e.proto.flow_id()];
        call.flow_id = e.proto.flow_id();
        call.events.push_back(e);

        if (call.start_ns == 0 || e.proto.timestamp_ns() < call.start_ns) {
            call.start_ns = e.proto.timestamp_ns();
        }
        call.end_ns = std::max(e.proto.timestamp_ns(), call.end_ns);

        if (e.proto.has_grpc()) {
            const auto& grpc = e.proto.grpc();
            if (grpc.status_code() != android::control::breadcrumbs::GrpcPayload::OK &&
                (grpc.grpc_phase() == android::control::breadcrumbs::GrpcPayload::PRE_RECV_STATUS ||
                 grpc.grpc_phase() == android::control::breadcrumbs::GrpcPayload::PRE_SEND_STATUS ||
                 grpc.grpc_phase() == android::control::breadcrumbs::GrpcPayload::END_OF_CALL)) {
                call.has_error = true;
            }
        }
    }
    VLOG(1) << "TraceAggregator: Found " << thread_ids.size() << " unique threads and "
            << trace.calls.size() << " gRPC calls.";

    // Pass 2: Create Lanes (Prioritize crashing thread at index 0).
    absl::flat_hash_map<uint64_t, size_t> tid_to_lane_idx;
    auto add_lane = [&](uint64_t tid) {
        tid_to_lane_idx[tid] = trace.lanes.size();
        TraceLane lane;
        lane.thread_id = tid;
        lane.is_crashing_thread = (tid == crashing_thread_id);
        trace.lanes.push_back(std::move(lane));
    };

    if (thread_ids.count(crashing_thread_id)) {
        add_lane(crashing_thread_id);
    }
    for (const uint64_t tid : thread_ids) {
        if (tid != crashing_thread_id) {
            add_lane(tid);
        }
    }

    // Pass 3: Populate Lanes with event copies.
    for (const auto& e : events) {
        const size_t idx = tid_to_lane_idx[e.proto.thread_id()];
        trace.lanes[idx].events.push_back(e);
    }

    // Pass 4: Assign color slots for visual contrast.
    AssignColorSlots(&trace);

    LOG(INFO) << "TraceAggregator: Aggregation complete. Total lanes: " << trace.lanes.size();

    return trace;
}

void TraceAggregator::AssignColorSlots(DiagnosticTrace* trace) {
    std::vector<Boundary> boundaries;
    for (const auto& [cid, call] : trace->calls) {
        boundaries.push_back({call.start_ns, cid, true});
        boundaries.push_back({call.end_ns, cid, false});
    }
    std::ranges::sort(boundaries, [](const Boundary& a, const Boundary& b) {
        if (a.ts != b.ts) return a.ts < b.ts;
        return a.is_start < b.is_start;
    });
    VLOG(1) << "TraceAggregator: Processing " << boundaries.size()
            << " boundaries for color assignment.";

    goldfish::UniqueIdAllocator allocator;
    int max_slots = 0;
    for (const auto& b : boundaries) {
        auto& call = trace->calls[b.call_id];
        VLOG(2) << "TraceAggregator: Sweep-line: " << b << " for " << call;
        if (b.is_start) {
            // Allocate a slot (UniqueIdAllocator starts at 1)
            call.color_slot = allocator.Get();
            max_slots = std::max(max_slots, static_cast<int>(call.color_slot));
        } else {
            // Return the slot to the pool for immediate reuse.
            if (call.color_slot == 0) {
                LOG(WARNING) << "TraceAggregator: Warning, unseen color for call " << b.call_id
                             << "! This can only happen if the start and end times "
                                "for a call are equal (very unlikely). The lane might not be "
                                "colored accurately";
            } else {
                allocator.Put(call.color_slot);
            }
        }
    }
    VLOG(1) << "TraceAggregator: Assigned " << max_slots << " unique color slots.";
}

}  // namespace android::crashreport::breadcrumbs
