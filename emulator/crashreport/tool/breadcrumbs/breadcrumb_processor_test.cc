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
#include "android/crashreport/breadcrumbs/breadcrumb_processor.h"

#include <gtest/gtest.h>

#include <vector>

#include "absl/container/flat_hash_map.h"
#include "grpc_diagnostic.pb.h"

#include "android/crashreport/breadcrumbs/breadcrumb_trace.h"
#include "breadcrumb.pb.h"
#include "goldfish/circular_message_log.h"

namespace android::crashreport::breadcrumbs {

using android::control::breadcrumbs::Breadcrumb;
using android::control::breadcrumbs::GrpcPayload;

class BreadcrumbProcessorTest : public ::testing::Test {
  protected:
    std::unique_ptr<goldfish::proto_data_store::CircularMessageLog> log_writer_;

    void AddEntry(std::vector<uint8_t>& buffer, uint64_t flow_id, uint64_t tid, uint64_t ts,
                  uint32_t hash, GrpcPayload::GrpcPhase phase = GrpcPayload::START,
                  GrpcPayload::GrpcStatusCode status = GrpcPayload::OK) {
        Breadcrumb proto;
        proto.set_flow_id(flow_id);
        proto.set_thread_id(tid);
        proto.set_timestamp_ns(ts);

        auto* grpc = proto.mutable_grpc();
        grpc->set_method_hash(hash);
        grpc->set_grpc_phase(phase);
        grpc->set_status_code(status);

        if (!log_writer_) {
            auto log_or = goldfish::proto_data_store::CircularMessageLog::CreateWriter(
                    buffer.data(), buffer.size(), proto);
            if (log_or.ok()) {
                log_writer_ = std::move(*log_or);
            }
        }

        if (log_writer_) {
            (void)log_writer_->Push(proto);
        }
    }

    void SetUp() override { log_writer_.reset(); }
};

TEST_F(BreadcrumbProcessorTest, ProcessesFullPipeline) {
    std::vector<uint8_t> buffer(4096, 0);

    // Add a simple trace: Call 1 starts on TID 100, ends on TID 100.
    AddEntry(buffer, 1, 100, 1000, 0, GrpcPayload::START);
    AddEntry(buffer, 1, 100, 2000, 0, GrpcPayload::END_OF_CALL);

    // Render without color for easier string matching in tests
    std::string output = BreadcrumbProcessor::Process(
            buffer, 100, TraceRendererFactory::RenderFormat::kText, /*use_color=*/false);

    // Verify presence of essential components in output
    EXPECT_NE(output.find("*"), std::string::npos);
    EXPECT_NE(output.find("(T100)"), std::string::npos);
    EXPECT_NE(output.find("REL. TIME"), std::string::npos);
    EXPECT_NE(output.find("[1] END"), std::string::npos);
}

TEST_F(BreadcrumbProcessorTest, HandlesEmptyBufferGracefully) {
    std::vector<uint8_t> buffer(1024, 0);
    std::string output = BreadcrumbProcessor::Process(buffer, 123);
    EXPECT_EQ(output, "No breadcrumbs found in buffer.");
}

TEST_F(BreadcrumbProcessorTest, SupportsMermaidOutput) {
    std::vector<uint8_t> buffer(4096, 0);
    // Call 1 starts on 100, migrates to 200, ends on 200
    AddEntry(buffer, 1, 100, 1000, 0, GrpcPayload::START);
    AddEntry(buffer, 1, 200, 1500, 0, GrpcPayload::PRE_SEND_MESSAGE);
    AddEntry(buffer, 1, 200, 2000, 0, GrpcPayload::END_OF_CALL, GrpcPayload::INTERNAL);

    std::string output =
            BreadcrumbProcessor::Process(buffer, 200, TraceRendererFactory::RenderFormat::kMermaid);

    std::cerr << "--- BEGIN MERMAID OUTPUT ---\n" << output << "--- END MERMAID OUTPUT ---\n";

    EXPECT_NE(output.find("sequenceDiagram"), std::string::npos);
    EXPECT_NE(output.find("participant T0 as Thread 200 [*]"), std::string::npos);
    EXPECT_NE(output.find("participant T1 as Thread 100"), std::string::npos);
    EXPECT_NE(output.find("T1->>T0: +500ns | [1] SEND_MSG"), std::string::npos);  // Migration
    EXPECT_NE(output.find("Note over T0: +1us | [1] END"), std::string::npos);    // End of call
    EXPECT_NE(output.find("Note right of T0: 💥 FATAL EXCEPTION"), std::string::npos);
}

TEST_F(BreadcrumbProcessorTest, TranslatesThreadIdsWithMap) {
    std::vector<uint8_t> buffer(4096, 0);

    // Add a trace: Call 1 on TID 100.
    AddEntry(buffer, 1, 100, 1000, 0, GrpcPayload::START);

    absl::flat_hash_map<uint64_t, uint64_t> tid_map;
    tid_map[100] = 42;  // Map OS TID 100 to Breakpad Index 42

    std::string output = BreadcrumbProcessor::Process(
            buffer, 42, TraceRendererFactory::RenderFormat::kText, /*use_color=*/false, tid_map);

    // Verify that the mapped ID (42) appears instead of the original ID (100)
    EXPECT_NE(output.find("*"), std::string::npos);
    EXPECT_NE(output.find("(T42)"), std::string::npos);
    EXPECT_EQ(output.find("100"),
              std::string::npos);  // 100 should not be there anymore as a thread ID label
}

}  // namespace android::crashreport::breadcrumbs
