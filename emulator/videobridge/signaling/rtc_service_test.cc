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
#include "goldfish/videobridge/rtc_service.h"

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"

#include "goldfish/videobridge/switchboard.h"
#include "nlohmann/json.hpp"

namespace goldfish::videobridge {

namespace v2 = ::android::emulation::control::v2;

namespace {

class RtcServiceTest : public ::testing::Test {
  protected:
    void SetUp() override {
        switchboard_ = std::make_shared<Switchboard>(nullptr);
        service_ = std::make_unique<RtcService>(switchboard_);

        ::grpc::ServerBuilder builder;
        builder.AddListeningPort("localhost:0", ::grpc::InsecureServerCredentials(),
                                 &selected_port_);
        builder.RegisterService(service_.get());
        server_ = builder.BuildAndStart();
        ASSERT_NE(server_, nullptr);

        server_address_ = absl::StrCat("localhost:", selected_port_);
        channel_ = ::grpc::CreateChannel(server_address_, ::grpc::InsecureChannelCredentials());
        stub_ = v2::Rtc::NewStub(channel_);
    }

    void TearDown() override {
        if (server_) {
            server_->Shutdown();
        }
    }

    std::shared_ptr<Switchboard> switchboard_;
    std::unique_ptr<RtcService> service_;
    int selected_port_ = 0;
    std::string server_address_;
    std::unique_ptr<::grpc::Server> server_;
    std::shared_ptr<::grpc::Channel> channel_;
    std::unique_ptr<v2::Rtc::Stub> stub_;
};

TEST_F(RtcServiceTest, RequestRtcStreamCreatesSession) {
    v2::RtcStreamRequest request;
    auto* ice_config = request.mutable_ice_server_config();
    ice_config->set_ice_transport_policy("relay");
    auto* server = ice_config->add_ice_servers();
    server->add_urls("stun:stun.l.google.com:19302");

    ::grpc::ClientContext context;
    v2::RtcStreamResponse response;
    ::grpc::Status status = stub_->RequestRtcStream(&context, request, &response);

    ASSERT_TRUE(status.ok()) << "RPC failed: " << status.error_message();
    EXPECT_FALSE(response.id().guid().empty());

    // Verify it was connected to switchboard.
    // We can push an outgoing signaling message from the switchboard and check it queues.
    const std::string guid = response.id().guid();
    switchboard_->Send(guid, {{"type", "offer"}, {"sdp", "v=0"}});

    auto maybe_msg = switchboard_->NextMessage(guid, absl::Milliseconds(100));
    ASSERT_TRUE(maybe_msg.ok());
    auto parsed = nlohmann::json::parse(*maybe_msg);
    EXPECT_EQ(parsed["type"], "offer");
}

TEST_F(RtcServiceTest, SendJsepMessageRoutesPayload) {
    // 1. Initialize session first
    v2::RtcStreamRequest start_request;
    ::grpc::ClientContext start_context;
    v2::RtcStreamResponse start_response;
    ASSERT_TRUE(stub_->RequestRtcStream(&start_context, start_request, &start_response).ok());
    const std::string guid = start_response.id().guid();

    // 2. Mock Jsep message send
    v2::SendJsepMessageRequest request;
    request.mutable_jsep_msg()->mutable_id()->set_guid(guid);
    request.mutable_jsep_msg()->set_message("{\"type\":\"candidate\",\"candidate\":\"foo\"}");

    ::grpc::ClientContext send_context;
    v2::SendJsepMessageResponse response;
    ::grpc::Status status = stub_->SendJsepMessage(&send_context, request, &response);

    // Note: Since EmulatorClient is nullptr in Switchboard, the route will successfully parse the
    // JSON but drop it because there is no active WebRTC PeerConnection session instantiated
    // (Participant::Initialize creates it). But AcceptJsepMessage will return true since it parsed
    // successfully and resolved the participant queue. Let's verify status is OK.
    EXPECT_TRUE(status.ok()) << "RPC failed: " << status.error_message();
}

TEST_F(RtcServiceTest, ReceiveJsepMessageStreamDeliversUpdates) {
    // 1. Initialize session
    v2::RtcStreamRequest start_request;
    ::grpc::ClientContext start_context;
    v2::RtcStreamResponse start_response;
    ASSERT_TRUE(stub_->RequestRtcStream(&start_context, start_request, &start_response).ok());
    const std::string guid = start_response.id().guid();

    // 2. Open Server-Streaming Reader
    ::grpc::ClientContext read_context;
    v2::ReceiveJsepMessageRequest request;
    request.mutable_id()->set_guid(guid);
    auto reader = stub_->ReceiveJsepMessageStream(&read_context, request);
    ASSERT_NE(reader, nullptr);

    // 3. Push signaling updates from switchboard
    std::thread writer_thread([this, guid]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        switchboard_->Send(guid, {{"type", "candidate"}, {"candidate", "bar"}});
    });

    // 4. Read streamed messages
    std::vector<v2::ReceiveJsepMessageResponse> messages;
    v2::ReceiveJsepMessageResponse temp;
    // Read the single message we expect to be sent.
    if (reader->Read(&temp)) {
        messages.push_back(temp);
    }
    // Now cancel the stream to stop the server-side loop.
    read_context.TryCancel();

    while (reader->Read(&temp)) {
        messages.push_back(temp);
    }
    ::grpc::Status status = reader->Finish();
    EXPECT_EQ(status.error_code(), ::grpc::StatusCode::CANCELLED);

    writer_thread.join();

    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages[0].jsep_msg().id().guid(), guid);
    EXPECT_NE(messages[0].jsep_msg().message().find("\"candidate\""), std::string::npos);
}

TEST_F(RtcServiceTest, ReceiveJsepMessageUnaryBlocksAndRetrieves) {
    // 1. Initialize session
    v2::RtcStreamRequest start_request;
    ::grpc::ClientContext start_context;
    v2::RtcStreamResponse start_response;
    ASSERT_TRUE(stub_->RequestRtcStream(&start_context, start_request, &start_response).ok());
    const std::string guid = start_response.id().guid();

    // 2. Push message in background
    std::thread writer_thread([this, guid]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        switchboard_->Send(guid, {{"type", "answer"}, {"sdp", "yes"}});
    });

    // 3. Unary read
    ::grpc::ClientContext read_context;
    v2::ReceiveJsepMessageRequest request;
    request.mutable_id()->set_guid(guid);
    v2::ReceiveJsepMessageResponse response;
    ::grpc::Status status = stub_->ReceiveJsepMessage(&read_context, request, &response);

    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.jsep_msg().id().guid(), guid);
    EXPECT_NE(response.jsep_msg().message().find("\"answer\""), std::string::npos);

    writer_thread.join();
}

}  // namespace
}  // namespace goldfish::videobridge
