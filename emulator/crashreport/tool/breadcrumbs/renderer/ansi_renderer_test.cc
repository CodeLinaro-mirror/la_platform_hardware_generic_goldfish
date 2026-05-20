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
#include "android/crashreport/breadcrumbs/ansi_renderer.h"

#include <gtest/gtest.h>

#include "android/crashreport/breadcrumbs/breadcrumb_trace.h"
#include "android/crashreport/breadcrumbs/trace_aggregator.h"

namespace android::crashreport::breadcrumbs {

class AnsiRendererTest : public ::testing::Test {
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

TEST_F(AnsiRendererTest, RendersBasicTable) {
    std::vector<EnrichedBreadcrumb> events = {
        CreateEvent(1, 100, 1000000, "MethodA", GrpcBreadcrumb::START),
        CreateEvent(2, 200, 2000000, "MethodB", GrpcBreadcrumb::START),
        CreateEvent(1, 100, 3000000, "MethodA", GrpcBreadcrumb::END_OF_CALL),
    };

    auto trace = TraceAggregator::Aggregate(events, 200);
    std::string output = AnsiRenderer(false).Render(trace);

    // Basic structural checks
    EXPECT_NE(output.find("REL. TIME"), std::string::npos);
    EXPECT_NE(output.find("MethodA"), std::string::npos);
    EXPECT_NE(output.find("MethodB"), std::string::npos);
    EXPECT_NE(output.find("100"), std::string::npos);  // Thread ID in header
    EXPECT_NE(output.find("*"), std::string::npos);    // Crashing thread marker in header
}

TEST_F(AnsiRendererTest, IncludesAnsiColors) {
    std::vector<EnrichedBreadcrumb> events = {
        CreateEvent(1, 100, 1000000, "MethodA", GrpcBreadcrumb::START),
    };

    auto trace = TraceAggregator::Aggregate(events, 0);
    std::string output = AnsiRenderer(true).Render(trace);

    // Check for ANSI escape sequence start
    EXPECT_NE(output.find("\033["), std::string::npos);
}

TEST_F(AnsiRendererTest, HighlightsErrorsInRed) {
    std::vector<EnrichedBreadcrumb> events = {
        CreateEvent(1, 100, 1000000, "MethodA", GrpcBreadcrumb::END_OF_CALL,
                    GrpcBreadcrumb::INTERNAL),
    };

    auto trace = TraceAggregator::Aggregate(events, 0);
    std::string output = AnsiRenderer(true).Render(trace);

    // Red color code for error status
    EXPECT_NE(output.find("\033[31m"), std::string::npos);
}

TEST_F(AnsiRendererTest, ForensicVisualInspection) {
    // This test simulates a realistic sequence of keyboard events across multiple threads
    // with thread migration to verify the visual clarity of the ANSI output.
    // Scenario: User types "Go" (Shift+G, then o).

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

    // Add payloads to PRE_SEND_MESSAGE events
    events[1].resolved_payload = "key: 'Shift', eventType: keydown";
    events[5].resolved_payload = "key: 'G', eventType: keyup";
    events[10].resolved_payload = "key: 'o', eventType: keydown";

    auto trace = TraceAggregator::Aggregate(events, 3000);
    std::string output = AnsiRenderer(true).Render(trace);

    // Print to stderr for manual inspection during 'bazel test --test_output=all'
    std::cerr << "\n--- BEGIN ANSI RENDER INSPECTION ---\n"
              << output << "--- END ANSI RENDER INSPECTION ---\n"
              << std::endl;

    EXPECT_FALSE(output.empty());
}

TEST_F(AnsiRendererTest, RendersWithoutColor) {
    std::vector<EnrichedBreadcrumb> events = {
        CreateEvent(1, 100, 1000000, "MethodA", GrpcBreadcrumb::START),
        CreateEvent(1, 100, 2000000, "MethodA", GrpcBreadcrumb::END_OF_CALL,
                    GrpcBreadcrumb::INTERNAL),
    };

    auto trace = TraceAggregator::Aggregate(events, 100);
    std::string output = AnsiRenderer(false).Render(trace);

    // Should NOT contain ANSI escape sequences
    EXPECT_EQ(output.find("\033["), std::string::npos);

    EXPECT_NE(output.find("MethodA"), std::string::npos);
    EXPECT_NE(output.find("*"), std::string::npos);  // Crashing thread marker in header
}

}  // namespace android::crashreport::breadcrumbs
