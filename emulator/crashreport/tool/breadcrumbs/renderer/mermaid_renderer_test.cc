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

#include <gtest/gtest.h>

#include "android/crashreport/breadcrumbs/breadcrumb_trace.h"
#include "android/crashreport/breadcrumbs/trace_aggregator.h"

namespace android::crashreport::breadcrumbs {

class MermaidRendererTest : public ::testing::Test {
  protected:
    EnrichedBreadcrumb CreateEvent(uint32_t call_id, uint64_t tid, uint64_t ts,
                                   std::string_view method = "TestMethod",
                                   GrpcBreadcrumb::Phase phase = GrpcBreadcrumb::PRE_SEND_MESSAGE,
                                   GrpcBreadcrumb::GrpcStatusCode status = GrpcBreadcrumb::OK) {
        EnrichedBreadcrumb e;
        e.proto.set_call_id(call_id);
        e.proto.set_thread_id(tid);
        e.proto.set_timestamp_ns(ts);
        e.proto.set_phase(phase);
        e.proto.set_status_code(status);
        e.method_name = method;
        return e;
    }
};

TEST_F(MermaidRendererTest, ForensicVisualInspection) {
    // Scenario: User types "Go" (Shift+G, then o).
    // This is the same scenario used for AnsiRenderer to ensure parity.

    std::vector<EnrichedBreadcrumb> events = {
        // Call 101: sendKey(Shift Down) - Starts on 1000, ends on 2000
        CreateEvent(101, 1000, 1000000, "sendKey", GrpcBreadcrumb::START),
        CreateEvent(101, 1000, 1100000, "sendKey", GrpcBreadcrumb::PRE_SEND_MESSAGE),
        CreateEvent(101, 2000, 1300000, "sendKey", GrpcBreadcrumb::END_OF_CALL),  // Handover to IO

        // Call 102: getStatus - Parallel on 2000
        CreateEvent(102, 2000, 1200000, "getStatus", GrpcBreadcrumb::START),
        CreateEvent(102, 2000, 1600000, "getStatus", GrpcBreadcrumb::END_OF_CALL),

        // Call 103: sendKey(G Up) - Local to 1000
        CreateEvent(103, 1000, 1500000, "sendKey", GrpcBreadcrumb::START),
        CreateEvent(103, 1000, 1550000, "sendKey", GrpcBreadcrumb::PRE_SEND_MESSAGE),
        CreateEvent(103, 1000, 1900000, "sendKey", GrpcBreadcrumb::END_OF_CALL),

        // Call 104: sendKey(o Down) - Starts on 2000, lands on 3000 (Crash Site)
        CreateEvent(104, 2000, 1700000, "sendKey", GrpcBreadcrumb::START),
        CreateEvent(104, 3000, 1800000, "sendKey",
                    GrpcBreadcrumb::PRE_SEND_MESSAGE),  // Handover to Crash Site
        CreateEvent(104, 3000, 2000000, "sendKey", GrpcBreadcrumb::END_OF_CALL,
                    GrpcBreadcrumb::INTERNAL),
    };

    // Add payloads
    events[1].resolved_payload = "key: 'Shift', type: keydown";
    events[5].resolved_payload = "key: 'G', type: keyup";
    events[10].resolved_payload = "key: 'o', type: keydown";

    auto trace = TraceAggregator::Aggregate(events, 3000);
    std::string output = MermaidRenderer().Render(trace);

    std::cerr << "\n--- BEGIN MERMAID RENDER INSPECTION ---\n"
              << output << "\n--- END MERMAID RENDER INSPECTION ---\n"
              << std::endl;

    EXPECT_FALSE(output.empty());
}

}  // namespace android::crashreport::breadcrumbs
