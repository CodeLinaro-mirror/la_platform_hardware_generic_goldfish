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
#include "android/emulation/control/simple_async_grpc.h"

#include <gtest/gtest.h>

#include <functional>
#include <string>

#include "android/emulation/control/grpc_event_stream_support.h"
#include "goldfish/eventing/event_sources.h"

namespace android::emulation::control {

template <typename R>
class FakeReactor {
  public:
    virtual ~FakeReactor() = default;
    virtual void OnReadDone(bool ok) = 0;
    virtual void OnDone() {}

    void StartRead(R* msg) {
        start_read_called = true;
        last_read_msg = msg;
    }

    void Finish(grpc::Status status) {
        finish_called = true;
        finish_status = status;
    }

    bool start_read_called = false;
    bool finish_called = false;
    grpc::Status finish_status = grpc::Status::OK;
    R* last_read_msg = nullptr;
};

TEST(SimpleAsyncGrpcTest, ReadErrorDoesNotCallStartRead) {
    struct MyMessage {
        int value;
    };

    bool read_called = false;
    SimpleServerLambdaReader<MyMessage, FakeReactor<MyMessage>> reader([&](const MyMessage* msg) {
        read_called = true;
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "test error");
    });

    // Simulate gRPC calling OnReadDone with ok=true
    reader.OnReadDone(true);

    EXPECT_TRUE(read_called);
    EXPECT_TRUE(reader.finish_called);
    EXPECT_EQ(reader.finish_status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);

    // This should be false, if it is true, we will crash.
    EXPECT_FALSE(reader.start_read_called);
}

TEST(SimpleAsyncGrpcTest, ReadSuccessCallsStartRead) {
    struct MyMessage {
        int value;
    };

    bool read_called = false;
    SimpleServerLambdaReader<MyMessage, FakeReactor<MyMessage>> reader([&](const MyMessage* msg) {
        read_called = true;
        return grpc::Status::OK;
    });

    // Simulate gRPC calling OnReadDone with ok=true
    reader.OnReadDone(true);

    EXPECT_TRUE(read_called);
    EXPECT_FALSE(reader.finish_called);
    EXPECT_TRUE(reader.start_read_called);
}

template <typename W>
class FakeWriteReactor {
  public:
    virtual ~FakeWriteReactor() = default;
    virtual void OnWriteDone(bool ok) = 0;
    virtual void OnDone() {}

    void StartWrite(const W* msg) {
        start_write_called = true;
        last_write_msg = msg;
        written_messages.push_back(*msg);
    }

    void Finish(grpc::Status status) {
        finish_called = true;
        finish_status = status;
    }

    bool start_write_called = false;
    bool finish_called = false;
    grpc::Status finish_status = grpc::Status::OK;
    const W* last_write_msg = nullptr;
    std::vector<W> written_messages;
};

TEST(SimpleAsyncGrpcTest, QueueSizeTracksPendingWrites) {
    struct MyMessage {
        int value;
    };

    WithSimpleQueueWriter<FakeWriteReactor<MyMessage>, 0, 4> writer;
    const auto& const_writer = writer;
    EXPECT_EQ(const_writer.QueueSize(), 0);
    EXPECT_EQ(const_writer.RecycleQueueSize(), 0);

    writer.Write(MyMessage{1});
    EXPECT_EQ(const_writer.QueueSize(), 1);

    writer.Write(MyMessage{2});
    EXPECT_EQ(const_writer.QueueSize(), 2);

    writer.OnWriteDone(true);
    EXPECT_EQ(const_writer.QueueSize(), 1);
    EXPECT_EQ(const_writer.RecycleQueueSize(), 1);

    writer.OnWriteDone(true);
    EXPECT_EQ(const_writer.QueueSize(), 0);
    EXPECT_EQ(const_writer.RecycleQueueSize(), 2);
}

TEST(SimpleAsyncGrpcTest, RecyclingDisabledByDefault) {
    struct BufferMessage {
        std::string payload;
    };

    WithSimpleQueueWriter<FakeWriteReactor<BufferMessage>> writer;
    EXPECT_EQ(writer.RecycleQueueSize(), 0);

    BufferMessage msg;
    msg.payload.resize(1024, 'a');
    writer.Write(std::move(msg));

    writer.OnWriteDone(true);
    EXPECT_EQ(writer.RecycleQueueSize(), 0);

    auto acquired = writer.AcquireMessage();
    EXPECT_TRUE(acquired.payload.empty());
}

TEST(SimpleAsyncGrpcTest, RecycleReusesBufferCapacity) {
    struct BufferMessage {
        std::string payload;
    };

    WithSimpleQueueWriter<FakeWriteReactor<BufferMessage>, /*max_queue_size=*/2,
                          /*recycle_size=*/2>
            writer;
    EXPECT_EQ(writer.RecycleQueueSize(), 0);

    BufferMessage msg;
    msg.payload.reserve(4096);
    msg.payload = "hello";
    writer.Write(std::move(msg));

    // First write is in flight
    EXPECT_EQ(writer.QueueSize(), 1);
    EXPECT_EQ(writer.RecycleQueueSize(), 0);

    // OnWriteDone moves it to recycled_queue_
    writer.OnWriteDone(true);
    EXPECT_EQ(writer.QueueSize(), 0);
    EXPECT_EQ(writer.RecycleQueueSize(), 1);

    // AcquireMessage retrieves the recycled message with preserved capacity
    BufferMessage recycled = writer.AcquireMessage();
    EXPECT_EQ(writer.RecycleQueueSize(), 0);
    EXPECT_GE(recycled.payload.capacity(), 4096);
}

TEST(SimpleAsyncGrpcTest, RecycledObjectRetainsStaleDataUntilRepopulated) {
    struct BufferMessage {
        std::string payload;
        int count{0};
    };

    WithSimpleQueueWriter<FakeWriteReactor<BufferMessage>, /*max_queue_size=*/2,
                          /*recycle_size=*/2>
            writer;

    BufferMessage msg;
    msg.payload = "stale_data_from_previous_send";
    msg.count = 42;
    writer.Write(std::move(msg));

    writer.OnWriteDone(true);
    EXPECT_EQ(writer.RecycleQueueSize(), 1);

    // AcquireMessage returns the object with stale data intact, enabling buffer reuse
    BufferMessage recycled = writer.AcquireMessage();
    EXPECT_EQ(recycled.payload, "stale_data_from_previous_send");
    EXPECT_EQ(recycled.count, 42);

    // Caller / populate_fn is responsible for properly re-initializing it
    recycled.payload = "fresh_data";
    recycled.count = 1;
    writer.Write(std::move(recycled));
    EXPECT_EQ(writer.last_write_msg->payload, "fresh_data");
    EXPECT_EQ(writer.last_write_msg->count, 1);
}

TEST(SimpleAsyncGrpcTest, RecycleQueueBoundedByRecycleSize) {
    struct BufferMessage {
        int id{0};
    };

    WithSimpleQueueWriter<FakeWriteReactor<BufferMessage>, /*max_queue_size=*/5,
                          /*recycle_size=*/2>
            writer;

    writer.Write(BufferMessage{1});
    writer.Write(BufferMessage{2});
    writer.Write(BufferMessage{3});

    writer.OnWriteDone(true);
    EXPECT_EQ(writer.RecycleQueueSize(), 1);

    writer.OnWriteDone(true);
    EXPECT_EQ(writer.RecycleQueueSize(), 2);
}

TEST(SimpleAsyncGrpcTest, MaxQueueSizeReplacesPendingUnwrittenMessage) {
    struct Frame {
        int frame_id{0};
    };

    WithSimpleQueueWriter<FakeWriteReactor<Frame>, /*max_queue_size=*/2> writer;

    writer.Write(Frame{1});  // In flight
    EXPECT_EQ(writer.QueueSize(), 1);
    ASSERT_NE(writer.last_write_msg, nullptr);
    EXPECT_EQ(writer.last_write_msg->frame_id, 1);

    writer.Write(Frame{2});  // 1st pending in queue
    EXPECT_EQ(writer.QueueSize(), 2);

    writer.Write(Frame{3});  // 2nd pending in queue (total size 3: 1 in-flight + 2 pending)
    EXPECT_EQ(writer.QueueSize(), 3);

    writer.Write(Frame{4});  // Drops Frame 3 (latest pending), replaces with Frame 4
    EXPECT_EQ(writer.QueueSize(), 3);

    writer.OnWriteDone(true);  // Frame 1 completes -> Frame 2 is dispatched
    EXPECT_EQ(writer.QueueSize(), 2);
    EXPECT_EQ(writer.last_write_msg->frame_id, 2);

    writer.OnWriteDone(true);  // Frame 2 completes -> Frame 4 is dispatched
    EXPECT_EQ(writer.QueueSize(), 1);
    EXPECT_EQ(writer.last_write_msg->frame_id, 4);

    writer.OnWriteDone(true);  // Frame 4 completes -> Queue empty
    EXPECT_EQ(writer.QueueSize(), 0);
}

TEST(SimpleAsyncGrpcTest, MaxQueueSizeOneReplacesPendingUnwrittenMessage) {
    struct Frame {
        int frame_id{0};
    };

    WithSimpleQueueWriter<FakeWriteReactor<Frame>, /*max_queue_size=*/1> writer;

    writer.Write(Frame{1});  // In flight
    EXPECT_EQ(writer.QueueSize(), 1);
    ASSERT_NE(writer.last_write_msg, nullptr);
    EXPECT_EQ(writer.last_write_msg->frame_id, 1);

    writer.Write(Frame{2});  // 1st pending in queue
    EXPECT_EQ(writer.QueueSize(), 2);

    writer.Write(Frame{3});  // Drops Frame 2, replaces with Frame 3
    EXPECT_EQ(writer.QueueSize(), 2);

    writer.OnWriteDone(true);  // Frame 1 completes -> Frame 3 is dispatched
    EXPECT_EQ(writer.QueueSize(), 1);
    EXPECT_EQ(writer.last_write_msg->frame_id, 3);

    writer.OnWriteDone(true);  // Frame 3 completes -> Queue empty
    EXPECT_EQ(writer.QueueSize(), 0);
}

TEST(SimpleAsyncGrpcTest, EvictedMessageIsMovedToRecycleQueue) {
    struct BufferMessage {
        std::string payload;
    };

    WithSimpleQueueWriter<FakeWriteReactor<BufferMessage>, /*max_queue_size=*/1,
                          /*recycle_size=*/2>
            writer;

    BufferMessage msg1;
    msg1.payload = "first_in_flight";
    writer.Write(std::move(msg1));

    BufferMessage msg2;
    msg2.payload.reserve(2048);
    msg2.payload = "second_pending";
    writer.Write(std::move(msg2));
    EXPECT_EQ(writer.RecycleQueueSize(), 0);

    BufferMessage msg3;
    msg3.payload = "third_replaces_second";
    writer.Write(std::move(msg3));

    // The evicted msg2 should now be in recycled_queue_ with preserved capacity
    EXPECT_EQ(writer.RecycleQueueSize(), 1);
    auto recycled = writer.AcquireMessage();
    EXPECT_GE(recycled.payload.capacity(), 2048);
    EXPECT_EQ(writer.RecycleQueueSize(), 0);
}

TEST(SimpleAsyncGrpcTest, LvalueAndRvalueWriteProduceEquivalentState) {
    struct MyMessage {
        int val{0};
    };

    WithSimpleQueueWriter<FakeWriteReactor<MyMessage>> writer;

    const MyMessage msg1{10};
    writer.Write(msg1);  // lvalue overload
    EXPECT_EQ(writer.QueueSize(), 1);
    EXPECT_EQ(writer.last_write_msg->val, 10);

    writer.OnWriteDone(true);
    EXPECT_EQ(writer.QueueSize(), 0);

    MyMessage msg2{20};
    writer.Write(std::move(msg2));  // rvalue overload
    EXPECT_EQ(writer.QueueSize(), 1);
    EXPECT_EQ(writer.last_write_msg->val, 20);
}

TEST(SimpleAsyncGrpcTest, ErrorServerWriterInstantiatesAndFinishes) {
    struct TestMsg {
        int val{0};
    };
    auto* error_writer = new ::ErrorServerWriter<TestMsg>(
            ::grpc::Status(::grpc::StatusCode::NOT_FOUND, "Not found"));
    ASSERT_NE(error_writer, nullptr);
    error_writer->OnDone();
}

TEST(SimpleAsyncGrpcTest, StateStreamWriterImmediateSnapshotAndFilter) {
    struct StateMsg {
        int value{0};
    };

    android::base::eventing::CallbackEventSource<int> source;
    int current_state = 100;
    int populate_invocations = 0;

    auto* writer = new StateStreamWriter<StateMsg, int>(
            &source,
            [&](StateMsg* msg) {
                populate_invocations++;
                msg->value = current_state;
            },
            [](int ev) { return ev > 0; });

    // 1. Initial snapshot must be taken immediately on construction
    EXPECT_EQ(populate_invocations, 1);

    // 2. Events filtered out by predicate (ev <= 0) must not trigger populate
    source.FireEvent(-5);
    EXPECT_EQ(populate_invocations, 1);

    // 3. Events accepted by predicate (ev > 0) must trigger populate
    current_state = 200;
    source.FireEvent(10);
    EXPECT_EQ(populate_invocations, 2);

    // 4. Stream completion unregisters callback from event source
    writer->OnDone();
    source.FireEvent(30);
    EXPECT_EQ(populate_invocations, 2);
}

}  // namespace android::emulation::control
