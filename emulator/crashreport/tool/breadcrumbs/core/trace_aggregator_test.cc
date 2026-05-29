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

#include <gtest/gtest.h>

#include "android/crashreport/breadcrumbs/breadcrumb_trace.h"

namespace android::crashreport::breadcrumbs {

using android::control::breadcrumbs::Breadcrumb;
using android::control::breadcrumbs::GrpcPayload;

class TraceAggregatorTest : public ::testing::Test {
  protected:
    EnrichedBreadcrumb CreateEvent(uint64_t flow_id, uint64_t tid, uint64_t ts,
                                   GrpcPayload::GrpcPhase phase = GrpcPayload::PRE_SEND_MESSAGE,
                                   GrpcPayload::GrpcStatusCode status = GrpcPayload::OK) {
        EnrichedBreadcrumb e;
        e.proto.set_flow_id(flow_id);
        e.proto.set_thread_id(tid);
        e.proto.set_timestamp_ns(ts);

        auto* grpc = e.proto.mutable_grpc();
        grpc->set_method_hash(0);
        grpc->set_grpc_phase(phase);
        grpc->set_status_code(status);
        return e;
    }
};

TEST_F(TraceAggregatorTest, HandlesEmptyInput) {
    std::vector<EnrichedBreadcrumb> events;
    auto trace = TraceAggregator::Aggregate(events, 123);

    EXPECT_EQ(trace.lanes.size(), 0);
    EXPECT_EQ(trace.calls.size(), 0);
    EXPECT_EQ(trace.global_start_ns, 0);
}

TEST_F(TraceAggregatorTest, CrashingThreadIsAlwaysFirstLane) {
    std::vector<EnrichedBreadcrumb> events = {
        CreateEvent(1, 10, 1000),  // Thread 10
        CreateEvent(2, 20, 2000),  // Thread 20
        CreateEvent(3, 30, 3000),  // Thread 30
    };

    // Case 1: Thread 20 is crashing
    auto trace1 = TraceAggregator::Aggregate(events, 20);
    ASSERT_GE(trace1.lanes.size(), 1);
    EXPECT_EQ(trace1.lanes[0].thread_id, 20);
    EXPECT_TRUE(trace1.lanes[0].is_crashing_thread);

    // Case 2: Thread 30 is crashing
    auto trace2 = TraceAggregator::Aggregate(events, 30);
    ASSERT_GE(trace2.lanes.size(), 1);
    EXPECT_EQ(trace2.lanes[0].thread_id, 30);
}

TEST_F(TraceAggregatorTest, ReconstructsCallLifecycle) {
    std::vector<EnrichedBreadcrumb> events = {
        CreateEvent(1, 10, 1000),  // Start
        CreateEvent(1, 20, 1500),  // Migration
        CreateEvent(1, 20, 2000),  // End
    };

    auto trace = TraceAggregator::Aggregate(events, 0);

    ASSERT_EQ(trace.calls.size(), 1);
    auto& call = trace.calls[1];
    EXPECT_EQ(call.flow_id, 1);
    EXPECT_EQ(call.events.size(), 3);
    EXPECT_EQ(call.start_ns, 1000);
    EXPECT_EQ(call.end_ns, 2000);
}

TEST_F(TraceAggregatorTest, DetectsErrorsInLifecycle) {
    std::vector<EnrichedBreadcrumb> events = {
        CreateEvent(1, 10, 1000, GrpcPayload::PRE_SEND_MESSAGE, GrpcPayload::OK),
        CreateEvent(1, 10, 2000, GrpcPayload::END_OF_CALL, GrpcPayload::UNAVAILABLE),
    };

    auto trace = TraceAggregator::Aggregate(events, 0);
    EXPECT_TRUE(trace.calls[1].has_error);
}

TEST_F(TraceAggregatorTest, ComplexColorSlotRecycling) {
    // A: 1000 -> 2000
    // B: 1500 -> 2500 (Overlaps A)
    // C: 3000 -> 4000 (Overlaps nothing)
    // D: 3500 -> 4500 (Overlaps C)

    std::vector<EnrichedBreadcrumb> events = {
        CreateEvent(1, 10, 1000), CreateEvent(1, 10, 2000),  // A
        CreateEvent(2, 10, 1500), CreateEvent(2, 10, 2500),  // B
        CreateEvent(3, 10, 3000), CreateEvent(3, 10, 4000),  // C
        CreateEvent(4, 10, 3500), CreateEvent(4, 10, 4500),  // D
    };

    auto trace = TraceAggregator::Aggregate(events, 0);

    // A and B must be different
    EXPECT_NE(trace.calls[1].color_slot, trace.calls[2].color_slot);

    // C can recycle A's slot because A ended at 2000 and C starts at 3000
    EXPECT_EQ(trace.calls[1].color_slot, trace.calls[3].color_slot);

    // D and C must be different
    EXPECT_NE(trace.calls[3].color_slot, trace.calls[4].color_slot);

    // D can recycle B's slot
    EXPECT_EQ(trace.calls[2].color_slot, trace.calls[4].color_slot);
}

TEST_F(TraceAggregatorTest, HandlesSimultaneousEvents) {
    // Two different calls starting at the exact same nanosecond
    std::vector<EnrichedBreadcrumb> events = {
        CreateEvent(1, 10, 1000),
        CreateEvent(1, 10, 2000),
        CreateEvent(2, 10, 1000),
        CreateEvent(2, 10, 2000),
    };

    auto trace = TraceAggregator::Aggregate(events, 0);

    // They must still get different slots
    EXPECT_NE(trace.calls[1].color_slot, trace.calls[2].color_slot);
}

}  // namespace android::crashreport::breadcrumbs
