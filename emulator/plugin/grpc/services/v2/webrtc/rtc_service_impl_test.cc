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

#include "webrtc/rtc_service_impl.h"

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "goldfish/videobridge/switchboard.h"
#include "nlohmann/json.hpp"
#include "webrtc/rtc_service.grpc.pb.h"

namespace goldfish::grpc::v2 {
namespace {

using ::android::emulation::v2::webrtc::JsepMessage;
using ::android::emulation::v2::webrtc::RtcSession;
using ::android::emulation::v2::webrtc::RtcStreamRequest;
using ::android::emulation::v2::webrtc::RtcStreamResponse;
using ::android::emulation::v2::webrtc::RtcStreamUpdateRequest;
using ::android::emulation::v2::webrtc::TrackType;
using ::goldfish::videobridge::Switchboard;
using ::google::protobuf::Empty;

class TestRtcServiceImpl : public RtcServiceImpl {
  public:
    using RtcServiceImpl::AwaitSessionAttached;
    using RtcServiceImpl::AwaitSessionDetached;
    using RtcServiceImpl::RtcServiceImpl;
};

class RtcServiceImplTest : public ::testing::Test {
  protected:
    void SetUp() override {
        switchboard_ = std::make_shared<Switchboard>(nullptr);
        now_ = absl::Now();
        StartServer(std::make_unique<TestRtcServiceImpl>(switchboard_));
    }

    void TearDown() override { StopServer(); }

    void StartServer(std::unique_ptr<TestRtcServiceImpl> service) {
        StopServer();
        service_ = std::move(service);
        if (service_) {
            ::grpc::ServerBuilder builder;
            builder.RegisterService(service_.get());
            server_ = builder.BuildAndStart();
            ASSERT_NE(server_, nullptr);
            channel_ = server_->InProcessChannel(::grpc::ChannelArguments());
            stub_ = ::android::emulation::v2::webrtc::RtcService::NewStub(channel_);
        }
    }

    void StopServer() {
        stub_.reset();
        channel_.reset();
        if (server_) {
            server_->Shutdown();
            server_->Wait();
            server_.reset();
        }
        service_.reset();
    }

    // Creates a session through the service and returns its id.
    std::string StartSession() {
        RtcStreamRequest request;
        ::grpc::ClientContext context;
        RtcStreamResponse response;
        EXPECT_TRUE(stub_->RequestRtcStream(&context, request, &response).ok());
        return response.handle().session_id();
    }

    absl::Time now_ = absl::Now();
    std::shared_ptr<Switchboard> switchboard_;
    std::unique_ptr<TestRtcServiceImpl> service_;
    std::unique_ptr<::grpc::Server> server_;
    std::shared_ptr<::grpc::Channel> channel_;
    std::unique_ptr<::android::emulation::v2::webrtc::RtcService::Stub> stub_;
};

TEST_F(RtcServiceImplTest, RequestRtcStreamCreatesSession) {
    RtcStreamRequest request;
    request.add_requested_tracks(TrackType::TRACK_TYPE_SCREEN_PRIMARY);
    request.add_requested_tracks(TrackType::TRACK_TYPE_AUDIO_SPEAKER);

    ::grpc::ClientContext context;
    RtcStreamResponse response;
    ::grpc::Status status = stub_->RequestRtcStream(&context, request, &response);

    ASSERT_TRUE(status.ok()) << "RPC failed: " << status.error_message();
    const std::string session_id = response.handle().session_id();
    EXPECT_EQ(session_id.length(), 36);
    EXPECT_TRUE(switchboard_->HasSession(session_id));

    switchboard_->Send(session_id, {{"type", "offer"}, {"sdp", "v=0"}});
    auto maybe_msg = switchboard_->NextMessage(session_id, absl::Milliseconds(100));
    ASSERT_TRUE(maybe_msg.ok());
    EXPECT_EQ(nlohmann::json::parse(*maybe_msg)["type"], "offer");
}

TEST_F(RtcServiceImplTest, SendJsepMessageRoutesPayload) {
    const std::string session_id = StartSession();

    JsepMessage request;
    request.mutable_handle()->set_session_id(session_id);
    request.set_message("{\"type\":\"candidate\",\"candidate\":\"foo\"}");

    ::grpc::ClientContext send_context;
    Empty response;
    ::grpc::Status status = stub_->SendJsepMessage(&send_context, request, &response);

    EXPECT_TRUE(status.ok()) << "RPC failed: " << status.error_message();
}

TEST_F(RtcServiceImplTest, SendJsepMessageEmptySessionReturnsInvalidArgument) {
    JsepMessage request;
    request.set_message("{}");

    ::grpc::ClientContext context;
    Empty response;
    ::grpc::Status status = stub_->SendJsepMessage(&context, request, &response);

    EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_F(RtcServiceImplTest, SendJsepMessageUnknownSessionReturnsNotFound) {
    JsepMessage request;
    request.mutable_handle()->set_session_id("does-not-exist");
    request.set_message("{}");

    ::grpc::ClientContext context;
    Empty response;
    ::grpc::Status status = stub_->SendJsepMessage(&context, request, &response);

    EXPECT_EQ(status.error_code(), ::grpc::StatusCode::NOT_FOUND);
}

TEST_F(RtcServiceImplTest, ReceiveJsepMessageStreamDeliversUpdates) {
    const std::string session_id = StartSession();
    RtcSession request;
    request.set_session_id(session_id);
    ::grpc::ClientContext context;
    auto reader = stub_->ReceiveJsepMessageStream(&context, request);

    switchboard_->Send(session_id, {{"type", "candidate"}, {"candidate", "bar"}});

    JsepMessage message;
    ASSERT_TRUE(reader->Read(&message));
    EXPECT_EQ(message.handle().session_id(), session_id);
    EXPECT_EQ(nlohmann::json::parse(message.message())["candidate"], "bar");

    switchboard_->Disconnect(session_id);
    EXPECT_TRUE(reader->Finish().ok());
}

TEST_F(RtcServiceImplTest, ReceiveJsepMessageStreamDrainsBacklogWithoutRecursing) {
    // The previous callback based implementation re-armed itself from its own
    // completion, so a backlog cost one stack frame per message and crashed here.
    constexpr size_t kBacklog = 1000;
    // A backlog builds whenever messages are produced before a client attaches.

    const std::string session_id = StartSession();
    for (size_t i = 0; i < kBacklog; ++i) {
        switchboard_->Send(session_id, {{"seq", i}});
    }

    RtcSession request;
    request.set_session_id(session_id);
    ::grpc::ClientContext context;
    auto reader = stub_->ReceiveJsepMessageStream(&context, request);

    size_t count = 0;
    JsepMessage message;
    while (count < kBacklog && reader->Read(&message)) {
        ++count;
    }
    EXPECT_EQ(count, kBacklog);

    switchboard_->Disconnect(session_id);
    EXPECT_TRUE(reader->Finish().ok());
}

TEST_F(RtcServiceImplTest, ReceiveJsepMessageStreamFinishesOnDisconnect) {
    const std::string session_id = StartSession();
    RtcSession request;
    request.set_session_id(session_id);
    ::grpc::ClientContext context;
    auto reader = stub_->ReceiveJsepMessageStream(&context, request);

    service_->AwaitSessionAttached(session_id);

    // Closing the session releases the blocked reader immediately; if it did not,
    // this would stall for a poll interval at a time.
    switchboard_->Disconnect(session_id);

    JsepMessage message;
    EXPECT_FALSE(reader->Read(&message));

    // A closed session is ordinary teardown, not an error.
    EXPECT_TRUE(reader->Finish().ok());
}

TEST_F(RtcServiceImplTest, ReceiveJsepMessageStreamDisconnectsOnCancel) {
    const std::string session_id = StartSession();
    RtcSession request;
    request.set_session_id(session_id);
    ::grpc::ClientContext context;
    auto reader = stub_->ReceiveJsepMessageStream(&context, request);

    service_->AwaitSessionAttached(session_id);

    context.TryCancel();

    JsepMessage message;
    while (reader->Read(&message)) {
    }

    ::grpc::Status status = reader->Finish();
    EXPECT_EQ(status.error_code(), ::grpc::StatusCode::CANCELLED);
    service_->AwaitSessionDetached(session_id);
    EXPECT_FALSE(switchboard_->HasSession(session_id));
}

TEST_F(RtcServiceImplTest, ReceiveJsepMessageStreamRejectsEmptyAndUnknownSessions) {
    RtcSession empty_req;
    ::grpc::ClientContext empty_ctx;
    auto empty_reader = stub_->ReceiveJsepMessageStream(&empty_ctx, empty_req);
    JsepMessage dummy;
    EXPECT_FALSE(empty_reader->Read(&dummy));
    EXPECT_EQ(empty_reader->Finish().error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);

    RtcSession unknown_req;
    unknown_req.set_session_id("does-not-exist");
    ::grpc::ClientContext unknown_ctx;
    auto unknown_reader = stub_->ReceiveJsepMessageStream(&unknown_ctx, unknown_req);
    EXPECT_FALSE(unknown_reader->Read(&dummy));
    EXPECT_EQ(unknown_reader->Finish().error_code(), ::grpc::StatusCode::NOT_FOUND);
}

TEST_F(RtcServiceImplTest, SecondStreamOnSameSessionIsRejected) {
    const std::string session_id = StartSession();
    RtcSession request;
    request.set_session_id(session_id);

    ::grpc::ClientContext first_context;
    auto first_reader = stub_->ReceiveJsepMessageStream(&first_context, request);

    service_->AwaitSessionAttached(session_id);

    ::grpc::ClientContext second_context;
    auto second_reader = stub_->ReceiveJsepMessageStream(&second_context, request);

    JsepMessage second_msg;
    EXPECT_FALSE(second_reader->Read(&second_msg));
    EXPECT_EQ(second_reader->Finish().error_code(), ::grpc::StatusCode::ALREADY_EXISTS);

    switchboard_->Disconnect(session_id);
    EXPECT_TRUE(first_reader->Finish().ok());
}

TEST_F(RtcServiceImplTest, ConcurrentSendAndTeardownIsSafe) {
    const std::string session_id = StartSession();
    RtcSession request;
    request.set_session_id(session_id);

    ::grpc::ClientContext context;
    auto reader = stub_->ReceiveJsepMessageStream(&context, request);

    service_->AwaitSessionAttached(session_id);

    absl::Notification sending;
    std::thread sender([&]() {
        for (int i = 0; i < 256; ++i) {
            switchboard_->Send(session_id, {{"seq", i}});
            if (i == 0) {
                sending.Notify();
            }
        }
    });

    sending.WaitForNotification();
    switchboard_->Disconnect(session_id);

    sender.join();
    JsepMessage msg;
    while (reader->Read(&msg)) {
    }
    EXPECT_TRUE(reader->Finish().ok());
}

TEST_F(RtcServiceImplTest, UpdateRtcStreamValidatesSession) {
    const std::string session_id = StartSession();

    RtcStreamUpdateRequest request;
    Empty response;

    // Empty handle.
    {
        ::grpc::ClientContext context;
        EXPECT_EQ(stub_->UpdateRtcStream(&context, request, &response).error_code(),
                  ::grpc::StatusCode::INVALID_ARGUMENT);
    }

    // Unknown session.
    {
        request.mutable_handle()->set_session_id("does-not-exist");
        ::grpc::ClientContext context;
        EXPECT_EQ(stub_->UpdateRtcStream(&context, request, &response).error_code(),
                  ::grpc::StatusCode::NOT_FOUND);
    }

    // Known session, no track changes requested.
    {
        request.mutable_handle()->set_session_id(session_id);
        ::grpc::ClientContext context;
        EXPECT_TRUE(stub_->UpdateRtcStream(&context, request, &response).ok());
    }

    // Track changes are not supported by the backend yet.
    {
        request.add_added_tracks(TrackType::TRACK_TYPE_SCREEN_SECONDARY);
        ::grpc::ClientContext context;
        EXPECT_EQ(stub_->UpdateRtcStream(&context, request, &response).error_code(),
                  ::grpc::StatusCode::UNIMPLEMENTED);
    }
}

TEST_F(RtcServiceImplTest, UnclaimedSessionsAreReclaimed) {
    StartServer(std::make_unique<TestRtcServiceImpl>(switchboard_, absl::Seconds(60),
                                                     [this]() { return now_; }));

    RtcStreamRequest request;
    ::grpc::ClientContext context;
    RtcStreamResponse response;
    ASSERT_TRUE(stub_->RequestRtcStream(&context, request, &response).ok());
    const std::string abandoned = response.handle().session_id();
    ASSERT_TRUE(switchboard_->HasSession(abandoned));

    // Nothing observes a client vanishing between RequestRtcStream and the stream,
    // so the next request is what reclaims it.
    now_ += absl::Seconds(61);
    RtcStreamResponse second_response;
    ::grpc::ClientContext second_context;
    ASSERT_TRUE(stub_->RequestRtcStream(&second_context, request, &second_response).ok());

    EXPECT_FALSE(switchboard_->HasSession(abandoned));
    EXPECT_TRUE(switchboard_->HasSession(second_response.handle().session_id()));

    StopServer();
}

TEST_F(RtcServiceImplTest, ClaimedSessionsSurviveTheSweep) {
    StartServer(std::make_unique<TestRtcServiceImpl>(switchboard_, absl::Seconds(60),
                                                     [this]() { return now_; }));

    RtcStreamRequest request;
    ::grpc::ClientContext context;
    RtcStreamResponse response;
    ASSERT_TRUE(stub_->RequestRtcStream(&context, request, &response).ok());
    const std::string claimed = response.handle().session_id();

    RtcSession stream_request;
    stream_request.set_session_id(claimed);
    ::grpc::ClientContext stream_context;
    auto reader = stub_->ReceiveJsepMessageStream(&stream_context, stream_request);

    service_->AwaitSessionAttached(claimed);

    now_ += absl::Seconds(61);
    RtcStreamResponse second_response;
    ::grpc::ClientContext second_context;
    ASSERT_TRUE(stub_->RequestRtcStream(&second_context, request, &second_response).ok());

    EXPECT_TRUE(switchboard_->HasSession(claimed));

    switchboard_->Disconnect(claimed);
    EXPECT_TRUE(reader->Finish().ok());

    StopServer();
}

}  // namespace
}  // namespace goldfish::grpc::v2
