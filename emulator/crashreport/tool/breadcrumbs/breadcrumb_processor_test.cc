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

#include "grpc_diagnostic.pb.h"

#include "android/crashreport/breadcrumbs/breadcrumb_trace.h"
#include "goldfish/circular_message_log.h"

namespace android::crashreport::breadcrumbs {

class BreadcrumbProcessorTest : public ::testing::Test {
  protected:
    std::unique_ptr<goldfish::proto_data_store::CircularMessageLog> log_writer_;

    void AddEntry(std::vector<uint8_t>& buffer, uint32_t call_id, uint64_t tid, uint64_t ts,
                  uint32_t hash, GrpcBreadcrumb::Phase phase = GrpcBreadcrumb::START,
                  GrpcBreadcrumb::GrpcStatusCode status = GrpcBreadcrumb::OK) {
        GrpcBreadcrumb proto;
        proto.set_call_id(call_id);
        proto.set_thread_id(tid);
        proto.set_timestamp_ns(ts);
        proto.set_method_hash(hash);
        proto.set_phase(phase);
        proto.set_status_code(status);

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
    AddEntry(buffer, 1, 100, 1000, 0, GrpcBreadcrumb::START);
    AddEntry(buffer, 1, 100, 2000, 0, GrpcBreadcrumb::END_OF_CALL);

    // Render without color for easier string matching in tests
    std::string output = BreadcrumbProcessor::Process(
            buffer, 100, TraceRendererFactory::RenderFormat::kText, /*use_color=*/false);

    std::cerr << "--- BEGIN PROCESSOR OUTPUT ---\n" << output << "--- END PROCESSOR OUTPUT ---\n";

    // Verify presence of essential components in output
    EXPECT_NE(output.find("THREADS"), std::string::npos);
    EXPECT_NE(output.find("[*] 100"), std::string::npos);
    EXPECT_NE(output.find("REL. TIME"), std::string::npos);
    EXPECT_NE(output.find("[1] END"), std::string::npos);
}

TEST_F(BreadcrumbProcessorTest, HandlesEmptyBufferGracefully) {
    std::vector<uint8_t> buffer(1024, 0);
    std::string output = BreadcrumbProcessor::Process(buffer, 123);
    EXPECT_EQ(output, "No gRPC breadcrumbs found in buffer.");
}

TEST_F(BreadcrumbProcessorTest, SupportsMermaidOutput) {
    std::vector<uint8_t> buffer(4096, 0);
    // Call 1 starts on 100, migrates to 200, ends on 200
    AddEntry(buffer, 1, 100, 1000, 0, GrpcBreadcrumb::START);
    AddEntry(buffer, 1, 200, 1500, 0, GrpcBreadcrumb::PRE_SEND_MESSAGE);
    AddEntry(buffer, 1, 200, 2000, 0, GrpcBreadcrumb::END_OF_CALL, GrpcBreadcrumb::INTERNAL);

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

}  // namespace android::crashreport::breadcrumbs
