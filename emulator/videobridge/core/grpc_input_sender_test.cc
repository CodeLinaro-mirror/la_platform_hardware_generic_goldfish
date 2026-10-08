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

#include "core/grpc_input_sender.h"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <vector>

namespace goldfish::videobridge {
namespace {

template <typename EventT>
class FakeClientWriter : public ::grpc::ClientWriterInterface<EventT> {
  public:
    FakeClientWriter(std::vector<EventT>* events, int* finish_count, bool* fail_writes)
            : events_(events), finish_count_(finish_count), fail_writes_(fail_writes) {}

    bool Write(const EventT& msg, ::grpc::WriteOptions /*options*/) override {
        if (*fail_writes_) {
            return false;
        }
        events_->push_back(msg);
        return true;
    }

    bool WritesDone() override { return !*fail_writes_; }

    ::grpc::Status Finish() override {
        (*finish_count_)++;
        return *fail_writes_ ? ::grpc::Status(::grpc::StatusCode::UNAVAILABLE, "Stream broken")
                             : ::grpc::Status::OK;
    }

  private:
    std::vector<EventT>* events_;
    int* finish_count_;
    bool* fail_writes_;
};

class FakeInputEmulatorClient : public EmulatorClient {
  public:
    bool IsConnected() const override { return connected_; }

    std::unique_ptr<::grpc::ClientWriterInterface<InputEvent>> StreamInputEvent(
            ::grpc::ClientContext* context, ::google::protobuf::Empty* /*response*/) override {
        EXPECT_NE(context, nullptr);
        if (context != nullptr) {
            EXPECT_TRUE(context->deadline() == std::chrono::system_clock::time_point::max());
            context->set_deadline(std::chrono::system_clock::now() + std::chrono::hours(1));
        }
        v1_stream_open_count_++;
        return std::make_unique<FakeClientWriter<InputEvent>>(&v1_events_, &v1_finish_count_,
                                                              &fail_writes_);
    }

    std::unique_ptr<::grpc::ClientWriterInterface<V2InputEvent>> StreamV2InputEvents(
            ::grpc::ClientContext* context, V2StreamInputEventsResponse* /*response*/) override {
        EXPECT_NE(context, nullptr);
        if (context != nullptr) {
            EXPECT_TRUE(context->deadline() == std::chrono::system_clock::time_point::max());
            context->set_deadline(std::chrono::system_clock::now() + std::chrono::hours(1));
        }
        v2_stream_open_count_++;
        return std::make_unique<FakeClientWriter<V2InputEvent>>(&v2_events_, &v2_finish_count_,
                                                                &fail_writes_);
    }

    void SetFailWrites(bool fail) { fail_writes_ = fail; }

    int V1StreamOpenCount() const { return v1_stream_open_count_; }
    int V1FinishCount() const { return v1_finish_count_; }
    const std::vector<InputEvent>& V1Events() const { return v1_events_; }

    int V2StreamOpenCount() const { return v2_stream_open_count_; }
    int V2FinishCount() const { return v2_finish_count_; }
    const std::vector<V2InputEvent>& V2Events() const { return v2_events_; }

  private:
    bool connected_ = true;
    bool fail_writes_ = false;
    int v1_stream_open_count_ = 0;
    int v1_finish_count_ = 0;
    std::vector<InputEvent> v1_events_;
    int v2_stream_open_count_ = 0;
    int v2_finish_count_ = 0;
    std::vector<V2InputEvent> v2_events_;
};

TEST(GrpcInputSenderTest, V1RestartAllocatesFreshClientContextAndFinishesStream) {
    auto client = std::make_shared<FakeInputEmulatorClient>();
    GrpcInputSender sender(client, DataChannelLabel::kInput);

    ASSERT_TRUE(sender.Start().ok());
    EXPECT_EQ(client->V1StreamOpenCount(), 1);

    InputEvent event;
    event.mutable_key_event()->set_key("Enter");
    sender.SendEvent(event);
    EXPECT_EQ(client->V1Events().size(), 1U);

    sender.Stop();
    EXPECT_EQ(client->V1FinishCount(), 1);

    // Starting again after Stop() must allocate a fresh ClientContext.
    ASSERT_TRUE(sender.Start().ok());
    EXPECT_EQ(client->V1StreamOpenCount(), 2);
    sender.SendEvent(event);
    EXPECT_EQ(client->V1Events().size(), 2U);

    sender.Stop();
    EXPECT_EQ(client->V1FinishCount(), 2);
}

TEST(GrpcInputSenderTest, V1WriteFailureFinishesStreamAndAllowsReconnectWithFreshContext) {
    auto client = std::make_shared<FakeInputEmulatorClient>();
    GrpcInputSender sender(client, DataChannelLabel::kInput);

    ASSERT_TRUE(sender.Start().ok());
    EXPECT_EQ(client->V1StreamOpenCount(), 1);

    client->SetFailWrites(true);
    InputEvent event;
    event.mutable_key_event()->set_key("Escape");
    sender.SendEvent(event);

    // Failed write must call Finish() and reset the broken stream.
    EXPECT_EQ(client->V1FinishCount(), 1);

    // Re-opening after write failure must allocate a fresh ClientContext.
    client->SetFailWrites(false);
    ASSERT_TRUE(sender.Start().ok());
    EXPECT_EQ(client->V1StreamOpenCount(), 2);
    sender.SendEvent(event);
    EXPECT_EQ(client->V1Events().size(), 1U);
}

TEST(GrpcInputSenderTest, V2RestartAndWriteFailureFinishStreamAndAllocateFreshContext) {
    auto client = std::make_shared<FakeInputEmulatorClient>();
    GrpcInputSender sender(client, DataChannelLabel::kInputV2);

    ASSERT_TRUE(sender.Start().ok());
    EXPECT_EQ(client->V2StreamOpenCount(), 1);

    V2InputEvent event;
    event.mutable_pointer()->set_display_id(0);
    sender.SendV2Event(event);
    EXPECT_EQ(client->V2Events().size(), 1U);

    // Simulate broken stream during SendV2Event.
    client->SetFailWrites(true);
    sender.SendV2Event(event);
    EXPECT_EQ(client->V2FinishCount(), 1);

    // Reconnect and stop cleanly.
    client->SetFailWrites(false);
    ASSERT_TRUE(sender.Start().ok());
    EXPECT_EQ(client->V2StreamOpenCount(), 2);
    sender.SendV2Event(event);
    EXPECT_EQ(client->V2Events().size(), 2U);

    sender.Stop();
    EXPECT_EQ(client->V2FinishCount(), 2);
}

}  // namespace
}  // namespace goldfish::videobridge
