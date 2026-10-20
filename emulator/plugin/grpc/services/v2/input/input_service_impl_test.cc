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

#include "input/input_service_impl.h"

#include <gmock/gmock.h>
#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "android/emulation/control/keyboard/key_event_sender.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/display/test/fake_multi_display.h"
#include "goldfish/display/test/fake_pixman_display.h"
#include "standard-headers/linux/input-event-codes.h"
#include "test/grpc_v2_service_test.h"

// Necessary on Windows where winsock/windows headers define send as a macro.
#undef send

namespace goldfish::grpc::v2 {
namespace {

using ::android::emulation::control::KeyboardEvent;
using ::android::emulation::control::kMtsPointerUp;
using ::android::emulation::control::keyboard::IKeyEventSender;
using ::android::emulation::v2::AxisType;
using ::android::emulation::v2::InputActivity;
using ::android::emulation::v2::InputEvent;
using ::android::emulation::v2::InputService;
using ::android::emulation::v2::KeyAction;
using ::android::emulation::v2::PointerAction;
using ::android::emulation::v2::SendInputEventsRequest;
using ::android::emulation::v2::SendInputEventsResponse;
using ::android::emulation::v2::StreamInputActivityRequest;
using ::android::emulation::v2::StreamInputEventsResponse;
using ::goldfish::async::EventLoop;
using ::goldfish::async::FlowId;
using ::goldfish::async::LibuvEventLoop;
using ::goldfish::async::ThreadedEventLoop;
using ::goldfish::display::test::ActiveFakePixmanDisplay;
using ::goldfish::display::test::FakeMultiDisplay;
using ::goldfish::display::test::FakePixmanDisplay;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::FieldsAre;
using ::testing::HasSubstr;

class RecordingKeyEventSender : public IKeyEventSender {
  public:
    void send(const KeyboardEvent request) override {
        absl::MutexLock lock(&events_mutex_);
        events_.push_back(request);
    }

    std::vector<KeyboardEvent> Events() const {
        absl::MutexLock lock(&events_mutex_);
        return events_;
    }

    void AwaitEventCount(size_t expected_count) const {
        const auto has_count = [this, expected_count]() ABSL_SHARED_LOCKS_REQUIRED(events_mutex_) {
            return events_.size() >= expected_count;
        };
        absl::MutexLock lock(&events_mutex_);
        events_mutex_.Await(absl::Condition(&has_count));
    }

  private:
    mutable absl::Mutex events_mutex_;
    std::vector<KeyboardEvent> events_ ABSL_GUARDED_BY(events_mutex_);
};

class FakePacingClock {
  public:
    explicit FakePacingClock(absl::Time initial = absl::FromUnixNanos(1'000'000'000LL))
            : current_time_(initial) {}

    absl::Time Now() const {
        absl::MutexLock lock(&clock_mutex_);
        return current_time_;
    }

    void Sleep(absl::Duration d) {
        absl::MutexLock lock(&clock_mutex_);
        sleep_calls_.push_back(d);
        current_time_ += d;
    }

    std::vector<absl::Duration> SleepCalls() const {
        absl::MutexLock lock(&clock_mutex_);
        return sleep_calls_;
    }

  private:
    mutable absl::Mutex clock_mutex_;
    absl::Time current_time_ ABSL_GUARDED_BY(clock_mutex_);
    std::vector<absl::Duration> sleep_calls_ ABSL_GUARDED_BY(clock_mutex_);
};

class PacingEventLoop final : public EventLoop {
  public:
    PacingEventLoop(std::unique_ptr<EventLoop> inner, FakePacingClock& clock)
            : EventLoop("PacingEventLoop"), inner_(std::move(inner)), clock_(clock) {}

    void ShutdownTimers() override { inner_->ShutdownTimers(); }
    size_t WaitUntilIdle() override { return inner_->WaitUntilIdle(); }
    std::future<absl::Status> Shutdown() override { return inner_->Shutdown(); }
    bool IsOnLoopThread() const override { return inner_->IsOnLoopThread(); }

    std::shared_ptr<Timer> CreateTimer(RepeatingTask task) override {
        return std::make_shared<PacingTimer>(*inner_, clock_, std::move(task));
    }

  protected:
    absl::Status PostImmediately(Task task, FlowId /*flow_id*/) override {
        return inner_->Post(std::move(task));
    }
    absl::Status PostDelayed(Task task, absl::Duration delay, FlowId /*flow_id*/) override {
        clock_.Sleep(delay);
        return inner_->Post(std::move(task));
    }

  private:
    class PacingTimer final : public Timer, public std::enable_shared_from_this<PacingTimer> {
      public:
        PacingTimer(EventLoop& loop, FakePacingClock& clock, RepeatingTask task)
                : loop_(loop), clock_(clock), task_(std::move(task)) {}

        void Schedule(absl::Duration initial_delay, absl::Duration /*interval*/) override {
            clock_.Sleep(initial_delay);
            loop_.Post([self = shared_from_this()]() {
                     if (!self->cancelled_.load(std::memory_order_acquire)) {
                         self->task_();
                     }
                 }).IgnoreError();
        }

        void Cancel() override { cancelled_.store(true, std::memory_order_release); }

      private:
        EventLoop& loop_;
        FakePacingClock& clock_;
        RepeatingTask task_;
        std::atomic<bool> cancelled_{false};
    };

    std::unique_ptr<EventLoop> inner_;
    FakePacingClock& clock_;
};

class InputServiceImplTest : public ::testing::Test {
  protected:
    void SetUp() override {
        loop_ = std::make_unique<PacingEventLoop>(
                ThreadedEventLoop::Create(LibuvEventLoop::Create()), clock_);
        multi_display_ = std::make_unique<FakeMultiDisplay>(loop_.get());
        key_sender_ = std::make_shared<RecordingKeyEventSender>();
        StartServer(DefaultOptions());
    }

    void TearDown() override {
        StopServer();
        if (multi_display_) {
            for (auto display_weak : multi_display_->Displays()) {
                if (auto display = display_weak.lock()) {
                    static_cast<ActiveFakePixmanDisplay*>(display.get())->Stop();
                }
            }
        }
    }

    InputServiceImpl::Options DefaultOptions() {
        InputServiceImpl::Options opts;
        opts.event_loop = loop_.get();
        opts.now_fn = [this]() { return clock_.Now(); };
        opts.sleep_fn = [this](absl::Duration d) { clock_.Sleep(d); };
        return opts;
    }

    void StartServer(InputServiceImpl::Options opts) {
        StopServer();
        service_ =
                std::make_unique<InputServiceImpl>(*multi_display_, key_sender_, std::move(opts));
        ::grpc::ServerBuilder builder;
        builder.RegisterService(service_.get());
        server_ = builder.BuildAndStart();
        ASSERT_NE(server_, nullptr);
        channel_ = server_->InProcessChannel(::grpc::ChannelArguments());
        stub_ = InputService::NewStub(channel_);
    }

    void StopServer() {
        stub_.reset();
        channel_.reset();
        if (server_) {
            server_->Shutdown(absl::ToChronoTime(absl::Now() + absl::Seconds(1)));
            server_->Wait();
            server_.reset();
        }
        service_.reset();
    }

    FakePixmanDisplay* GetDisplay(uint8_t display_id = 0) {
        auto res = multi_display_->GetActiveDisplay(display_id, false);
        if (!res.ok()) {
            return nullptr;
        }
        return static_cast<FakePixmanDisplay*>(res.value().get());
    }

    // Acknowledges in-flight writes until every queued activity reaches `written_messages`.
    template <typename Writer>
    static void DrainActivity(Writer* writer) {
        size_t written = 0;
        while (written != writer->written_messages.size()) {
            written = writer->written_messages.size();
            writer->OnWriteDone(true);
        }
    }

    FakePacingClock clock_;
    std::unique_ptr<EventLoop> loop_;
    std::unique_ptr<FakeMultiDisplay> multi_display_;
    std::shared_ptr<RecordingKeyEventSender> key_sender_;
    std::unique_ptr<InputServiceImpl> service_;
    std::unique_ptr<::grpc::Server> server_;
    std::shared_ptr<::grpc::Channel> channel_;
    std::unique_ptr<InputService::Stub> stub_;
};

TEST_F(InputServiceImplTest, SendInputEventsEmptyBatchIsNoOp) {
    ::grpc::ServerContext ctx;
    SendInputEventsRequest req;
    SendInputEventsResponse resp;

    const auto status = service_->SendInputEvents(&ctx, &req, &resp);
    ASSERT_TRUE(status.ok()) << status.error_message();
    EXPECT_EQ(resp.events_dispatched(), 0);
    EXPECT_EQ(resp.first_event_dispatched_at_nanos(), 0);
    EXPECT_EQ(resp.last_event_dispatched_at_nanos(), 0);
    EXPECT_TRUE(clock_.SleepCalls().empty());
}

TEST_F(InputServiceImplTest, SendInputEventsPacesEventsAccordingToTimestamps) {
    ::grpc::ServerContext ctx;
    SendInputEventsRequest req;

    // 3-step gesture at t=100ms, t=115ms (+15ms), t=140ms (+25ms from previous, +40ms from start).
    auto* ev1 = req.add_events();
    ev1->set_timestamp_nanos(100'000'000LL);
    auto* ptr1 = ev1->mutable_pointer();
    ptr1->set_display_id(0);
    ptr1->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p1 = ptr1->add_pointers();
    p1->set_pointer_id(0);
    p1->set_x(0.5F);
    p1->set_y(0.5F);

    auto* ev2 = req.add_events();
    ev2->set_timestamp_nanos(115'000'000LL);
    auto* ptr2 = ev2->mutable_pointer();
    ptr2->set_display_id(0);
    ptr2->set_action(PointerAction::POINTER_ACTION_MOVE);
    auto* p2 = ptr2->add_pointers();
    p2->set_pointer_id(0);
    p2->set_x(0.6F);
    p2->set_y(0.6F);

    auto* ev3 = req.add_events();
    ev3->set_timestamp_nanos(140'000'000LL);
    auto* ptr3 = ev3->mutable_pointer();
    ptr3->set_display_id(0);
    ptr3->set_action(PointerAction::POINTER_ACTION_UP);
    auto* p3 = ptr3->add_pointers();
    p3->set_pointer_id(0);
    p3->set_x(0.6F);
    p3->set_y(0.6F);

    SendInputEventsResponse resp;
    const auto status = service_->SendInputEvents(&ctx, &req, &resp);
    ASSERT_TRUE(status.ok()) << status.error_message();

    EXPECT_EQ(resp.events_dispatched(), 3);
    EXPECT_EQ(resp.first_event_dispatched_at_nanos(), 1'000'000'000LL);
    EXPECT_EQ(resp.last_event_dispatched_at_nanos(), 1'040'000'000LL);

    const auto sleeps = clock_.SleepCalls();
    ASSERT_EQ(sleeps.size(), 2);
    EXPECT_EQ(sleeps[0], absl::Milliseconds(15));
    EXPECT_EQ(sleeps[1], absl::Milliseconds(25));
}

TEST_F(InputServiceImplTest, SendInputEventsValidatesBoundsUpFront) {
    // Decreasing timestamp_nanos must fail with INVALID_ARGUMENT without dispatching.
    {
        ::grpc::ServerContext ctx;
        SendInputEventsRequest req;
        auto* ev1 = req.add_events();
        ev1->set_timestamp_nanos(200'000'000LL);
        ev1->mutable_text()->set_text("a");
        auto* ev2 = req.add_events();
        ev2->set_timestamp_nanos(100'000'000LL);
        ev2->mutable_text()->set_text("b");

        SendInputEventsResponse resp;
        const auto status = service_->SendInputEvents(&ctx, &req, &resp);
        EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
        EXPECT_THAT(status.error_message(), HasSubstr("events[1]"));
        EXPECT_THAT(status.error_message(), HasSubstr("'100000000'"));
        EXPECT_THAT(status.error_message(), HasSubstr("'200000000'"));
        EXPECT_TRUE(key_sender_->Events().empty());
    }

    // Gesture exceeding max_batch_duration (default 30s) must fail with INVALID_ARGUMENT.
    {
        ::grpc::ServerContext ctx;
        SendInputEventsRequest req;
        auto* ev1 = req.add_events();
        ev1->set_timestamp_nanos(0);
        ev1->mutable_text()->set_text("a");
        auto* ev2 = req.add_events();
        ev2->set_timestamp_nanos(31'000'000'000LL);  // 31 seconds > 30s max
        ev2->mutable_text()->set_text("b");

        SendInputEventsResponse resp;
        const auto status = service_->SendInputEvents(&ctx, &req, &resp);
        EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
        EXPECT_THAT(status.error_message(), HasSubstr("'31s'"));
        EXPECT_THAT(status.error_message(), HasSubstr("'30s'"));
        EXPECT_THAT(status.error_message(), HasSubstr("StreamInputEvents"));
        EXPECT_TRUE(key_sender_->Events().empty());
    }

    // Missing event oneof payload must fail with INVALID_ARGUMENT.
    {
        ::grpc::ServerContext ctx;
        SendInputEventsRequest req;
        req.add_events();  // EVENT_NOT_SET

        SendInputEventsResponse resp;
        const auto status = service_->SendInputEvents(&ctx, &req, &resp);
        EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
        EXPECT_THAT(status.error_message(), HasSubstr("'pointer', 'key', 'axis', or 'text'"));
    }

    // Unknown display_id must fail with INVALID_ARGUMENT and include quoted display_id and
    // underlying cause.
    {
        ::grpc::ServerContext ctx;
        SendInputEventsRequest req;
        auto* ev = req.add_events();
        ev->mutable_pointer()->set_display_id(42);
        ev->mutable_pointer()->set_action(PointerAction::POINTER_ACTION_DOWN);

        SendInputEventsResponse resp;
        const auto status = service_->SendInputEvents(&ctx, &req, &resp);
        EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
        EXPECT_THAT(status.error_message(), HasSubstr("display_id '42'"));
        EXPECT_THAT(status.error_message(), HasSubstr("CapabilitiesService"));
    }

    // Unsupported Android keycode must fail with INVALID_ARGUMENT and list allowed keycodes.
    {
        ::grpc::ServerContext ctx;
        SendInputEventsRequest req;
        auto* ev = req.add_events();
        ev->mutable_key()->set_action(KeyAction::KEY_ACTION_PRESS);
        ev->mutable_key()->set_android_keycode(999);

        SendInputEventsResponse resp;
        const auto status = service_->SendInputEvents(&ctx, &req, &resp);
        EXPECT_EQ(status.error_code(), ::grpc::StatusCode::INVALID_ARGUMENT);
        EXPECT_THAT(status.error_message(), HasSubstr("android_keycode '999'"));
        EXPECT_THAT(status.error_message(), HasSubstr("3=HOME"));
    }
}

TEST_F(InputServiceImplTest, RejectsUnsupportedPeripheralsWithFailedPrecondition) {
    auto opts = DefaultOptions();
    opts.hw_keyboard = false;
    opts.hw_rotary_input = false;
    StartServer(std::move(opts));

    {
        ::grpc::ServerContext ctx;
        SendInputEventsRequest req;
        auto* ev = req.add_events();
        ev->mutable_key()->set_action(KeyAction::KEY_ACTION_PRESS);
        ev->mutable_key()->set_dom_code("KeyA");
        SendInputEventsResponse resp;
        const auto status = service_->SendInputEvents(&ctx, &req, &resp);
        EXPECT_EQ(status.error_code(), ::grpc::StatusCode::FAILED_PRECONDITION);
        EXPECT_THAT(status.error_message(), HasSubstr("hw.keyboard"));
    }

    {
        ::grpc::ServerContext ctx;
        SendInputEventsRequest req;
        auto* ev = req.add_events();
        ev->mutable_axis()->set_axis(AxisType::AXIS_TYPE_ROTARY_ENCODER);
        ev->mutable_axis()->set_value(1.0F);
        SendInputEventsResponse resp;
        const auto status = service_->SendInputEvents(&ctx, &req, &resp);
        EXPECT_EQ(status.error_code(), ::grpc::StatusCode::FAILED_PRECONDITION);
        EXPECT_THAT(status.error_message(), HasSubstr("hw.rotaryInput"));
    }
}

TEST_F(InputServiceImplTest, ReadOnlyModeRejectsWriteRpcsWithPermissionDenied) {
    auto opts = DefaultOptions();
    opts.read_only = true;
    StartServer(std::move(opts));

    ::grpc::ServerContext ctx;
    SendInputEventsRequest req;
    SendInputEventsResponse resp;
    const auto send_status = service_->SendInputEvents(&ctx, &req, &resp);
    EXPECT_EQ(send_status.error_code(), ::grpc::StatusCode::PERMISSION_DENIED);
    EXPECT_THAT(send_status.error_message(), HasSubstr("SendInputEvents"));

    ::grpc::ClientContext stream_ctx;
    StreamInputEventsResponse stream_resp;
    auto writer = stub_->StreamInputEvents(&stream_ctx, &stream_resp);
    writer->WritesDone();
    const auto stream_status = writer->Finish();
    EXPECT_EQ(stream_status.error_code(), ::grpc::StatusCode::PERMISSION_DENIED);
    EXPECT_THAT(stream_status.error_message(), HasSubstr("StreamInputEvents"));
}

TEST_F(InputServiceImplTest, ConcurrentSourcesWithSamePointerIdUseDistinctSlotsAndIsolateCleanup) {
    // Open an activity observer to capture all client and synthesized cleanup events.
    StreamInputActivityRequest activity_req;
    auto* activity_writer =
            service_->CreateInputActivityWriter<test::FakeWriteReactor<InputActivity>>(
                    activity_req);

    // Stream 1 uses x-client-id "agent-1" with input_source_id=0 (auto-derived non-zero ID).
    ::grpc::ClientContext ctx1;
    ctx1.AddMetadata("x-client-id", "agent-1");
    StreamInputEventsResponse resp1;
    auto writer1 = stub_->StreamInputEvents(&ctx1, &resp1);

    // Stream 2 uses x-client-id "agent-2" with input_source_id=0 (auto-derived non-zero ID).
    ::grpc::ClientContext ctx2;
    ctx2.AddMetadata("x-client-id", "agent-2");
    StreamInputEventsResponse resp2;
    auto writer2 = stub_->StreamInputEvents(&ctx2, &resp2);

    const int32_t expected_source1 = InputServiceImpl::DeriveSourceIdFromClientId("agent-1");
    const int32_t expected_source2 = InputServiceImpl::DeriveSourceIdFromClientId("agent-2");
    ASSERT_NE(expected_source1, 0);
    ASSERT_NE(expected_source2, 0);
    ASSERT_NE(expected_source1, expected_source2);

    // Agent 1 touches down with pointer_id = 0 and holds down KeyA.
    InputEvent down1;
    down1.mutable_pointer()->set_display_id(0);
    down1.mutable_pointer()->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p1 = down1.mutable_pointer()->add_pointers();
    p1->set_pointer_id(0);
    p1->set_x(0.2F);
    p1->set_y(0.2F);
    ASSERT_TRUE(writer1->Write(down1));

    InputEvent key_down1;
    key_down1.mutable_key()->set_action(KeyAction::KEY_ACTION_DOWN);
    key_down1.mutable_key()->set_dom_code("KeyA");
    ASSERT_TRUE(writer1->Write(key_down1));
    key_sender_->AwaitEventCount(1);

    // Agent 2 simultaneously touches down with pointer_id = 0 and sends a text marker.
    InputEvent down2;
    down2.mutable_pointer()->set_display_id(0);
    down2.mutable_pointer()->set_action(PointerAction::POINTER_ACTION_DOWN);
    auto* p2 = down2.mutable_pointer()->add_pointers();
    p2->set_pointer_id(0);
    p2->set_x(0.8F);
    p2->set_y(0.8F);
    ASSERT_TRUE(writer2->Write(down2));

    InputEvent text2;
    text2.mutable_text()->set_text("ready");
    ASSERT_TRUE(writer2->Write(text2));
    key_sender_->AwaitEventCount(2);

    // Close Stream 1 while its pointer 0 and KeyA are still depressed.
    ASSERT_TRUE(writer1->WritesDone());
    ASSERT_TRUE(writer1->Finish().ok());
    EXPECT_EQ(resp1.events_dispatched(), 2);

    DrainActivity(activity_writer);

    // Verify we observed:
    // 0. Agent 1 pointer DOWN (synthesized = false, source = expected_source1)
    // 1. Agent 1 KeyA DOWN (synthesized = false, source = expected_source1)
    // 2. Agent 2 pointer DOWN (synthesized = false, source = expected_source2)
    // 3. Agent 2 text "ready" (synthesized = false, source = expected_source2)
    // 4. Agent 1 synthesized POINTER_ACTION_CANCEL (synthesized = true, source = expected_source1)
    // 5. Agent 1 synthesized KEY_ACTION_UP (synthesized = true, source = expected_source1)
    ASSERT_EQ(activity_writer->written_messages.size(), 6);
    EXPECT_EQ(activity_writer->written_messages[0].event().input_source_id(), expected_source1);
    EXPECT_FALSE(activity_writer->written_messages[0].synthesized());
    EXPECT_EQ(activity_writer->written_messages[1].event().input_source_id(), expected_source1);
    EXPECT_FALSE(activity_writer->written_messages[1].synthesized());
    EXPECT_EQ(activity_writer->written_messages[2].event().input_source_id(), expected_source2);
    EXPECT_FALSE(activity_writer->written_messages[2].synthesized());
    EXPECT_EQ(activity_writer->written_messages[3].event().input_source_id(), expected_source2);
    EXPECT_FALSE(activity_writer->written_messages[3].synthesized());

    EXPECT_EQ(activity_writer->written_messages[4].event().input_source_id(), expected_source1);
    EXPECT_TRUE(activity_writer->written_messages[4].synthesized());
    EXPECT_EQ(activity_writer->written_messages[4].event().pointer().action(),
              PointerAction::POINTER_ACTION_CANCEL);
    ASSERT_EQ(activity_writer->written_messages[4].event().pointer().pointers_size(), 1);
    EXPECT_EQ(activity_writer->written_messages[4].event().pointer().pointers(0).pointer_id(), 0);

    EXPECT_EQ(activity_writer->written_messages[5].event().input_source_id(), expected_source1);
    EXPECT_TRUE(activity_writer->written_messages[5].synthesized());
    EXPECT_EQ(activity_writer->written_messages[5].event().key().action(),
              KeyAction::KEY_ACTION_UP);
    EXPECT_EQ(activity_writer->written_messages[5].event().key().dom_code(), "KeyA");

    // Verify Agent 1 and Agent 2 were assigned distinct guest multi-touch slots (0 and 1),
    // and only slot 0 was released when Agent 1 disconnected!
    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    const auto& evdev = display->evdevs;
    int slot0_releases = 0;
    int slot1_releases = 0;
    bool slot1_allocated = false;
    uint32_t current_slot = 999;
    for (const auto& ev : evdev) {
        if (ev.type == EV_ABS && ev.code == ABS_MT_SLOT) {
            current_slot = ev.value;
        } else if (ev.type == EV_ABS && ev.code == ABS_MT_TRACKING_ID) {
            if (current_slot == 1 && ev.value == 1U) {
                slot1_allocated = true;
            }
            if (ev.value == static_cast<uint32_t>(kMtsPointerUp)) {
                if (current_slot == 0) {
                    ++slot0_releases;
                } else if (current_slot == 1) {
                    ++slot1_releases;
                }
            }
        }
    }
    EXPECT_TRUE(slot1_allocated);
    EXPECT_EQ(slot0_releases, 1);
    EXPECT_EQ(slot1_releases, 0);

    // Now finish Stream 2; its held pointer on slot 1 should be released on exit.
    ASSERT_TRUE(writer2->WritesDone());
    ASSERT_TRUE(writer2->Finish().ok());
    EXPECT_EQ(resp2.events_dispatched(), 2);

    activity_writer->OnDone();
}

TEST_F(InputServiceImplTest, StreamDisconnectOnlyReleasesSourcesUsedByTheStream) {
    StreamInputActivityRequest activity_req;
    auto* activity_writer =
            service_->CreateInputActivityWriter<test::FakeWriteReactor<InputActivity>>(
                    activity_req);

    // The human operator (source 0) holds a touch down.
    InputEvent touch_down;
    touch_down.mutable_pointer()->set_action(PointerAction::POINTER_ACTION_DOWN);
    touch_down.mutable_pointer()->add_pointers()->set_pointer_id(0);
    ::grpc::ServerContext send_ctx;
    SendInputEventsRequest send_req;
    *send_req.add_events() = touch_down;
    SendInputEventsResponse send_resp;
    ASSERT_TRUE(service_->SendInputEvents(&send_ctx, &send_req, &send_resp).ok());

    // A stream without x-client-id touches down as source 7, then disconnects.
    ::grpc::ClientContext stream_ctx;
    StreamInputEventsResponse stream_resp;
    auto writer = stub_->StreamInputEvents(&stream_ctx, &stream_resp);
    touch_down.set_input_source_id(7);
    ASSERT_TRUE(writer->Write(touch_down));
    ASSERT_TRUE(writer->WritesDone());
    ASSERT_TRUE(writer->Finish().ok());

    // Only the stream's own contact is cancelled; the human's touch stays down.
    DrainActivity(activity_writer);
    std::vector<int32_t> synthesized_sources;
    for (const InputActivity& activity : activity_writer->written_messages) {
        if (activity.synthesized()) {
            synthesized_sources.push_back(activity.event().input_source_id());
        }
    }
    EXPECT_THAT(synthesized_sources, ElementsAre(7));
    activity_writer->OnDone();
}

TEST_F(InputServiceImplTest, PointerlessHoverExitReleasesHoveringContacts) {
    ::grpc::ServerContext ctx;
    SendInputEventsRequest req;
    auto* hover = req.add_events()->mutable_pointer();
    hover->set_action(PointerAction::POINTER_ACTION_HOVER_MOVE);
    hover->add_pointers()->set_pointer_id(0);
    req.add_events()->mutable_pointer()->set_action(PointerAction::POINTER_ACTION_HOVER_EXIT);
    SendInputEventsResponse resp;
    ASSERT_TRUE(service_->SendInputEvents(&ctx, &req, &resp).ok());

    auto* display = GetDisplay(0);
    ASSERT_NE(display, nullptr);
    EXPECT_THAT(display->evdevs, Contains(FieldsAre(EV_ABS, ABS_MT_TRACKING_ID, kMtsPointerUp)));
}

TEST_F(InputServiceImplTest, StreamInputActivityFiltersBySourceAndEnforcesObservationPolicy) {
    // Subscribe with source filter = 0 (human operator only).
    StreamInputActivityRequest filter_req;
    filter_req.set_input_source_id(0);
    auto* human_observer =
            service_->CreateInputActivityWriter<test::FakeWriteReactor<InputActivity>>(filter_req);

    // Send an agent event (source = 7) followed by a human event (source = 0).
    ::grpc::ServerContext ctx;
    SendInputEventsRequest req;
    auto* agent_ev = req.add_events();
    agent_ev->set_input_source_id(7);
    agent_ev->mutable_text()->set_text("agent");

    auto* human_ev = req.add_events();
    human_ev->set_input_source_id(0);
    human_ev->mutable_text()->set_text("human");

    SendInputEventsResponse resp;
    ASSERT_TRUE(service_->SendInputEvents(&ctx, &req, &resp).ok());

    ASSERT_EQ(human_observer->written_messages.size(), 1);
    EXPECT_EQ(human_observer->written_messages[0].event().input_source_id(), 0);
    EXPECT_EQ(human_observer->written_messages[0].event().text().text(), "human");
    human_observer->OnDone();

    // When supports_input_activity_observation is false, subscribing must fail with
    // PERMISSION_DENIED.
    auto opts = DefaultOptions();
    opts.supports_input_activity_observation = false;
    StartServer(std::move(opts));

    ::grpc::ClientContext denied_ctx;
    auto denied_reader = stub_->StreamInputActivity(&denied_ctx, StreamInputActivityRequest());
    InputActivity unused;
    EXPECT_FALSE(denied_reader->Read(&unused));
    const ::grpc::Status denied_status = denied_reader->Finish();
    EXPECT_EQ(denied_status.error_code(), ::grpc::StatusCode::PERMISSION_DENIED);
    EXPECT_THAT(denied_status.error_message(), HasSubstr("supports_input_activity_observation"));
}

TEST_F(InputServiceImplTest, StreamInputEventsPacesAndCapsStaleInterBurstDeltas) {
    ::grpc::ClientContext stream_ctx;
    StreamInputEventsResponse resp;
    auto writer = stub_->StreamInputEvents(&stream_ctx, &resp);

    // Event 1 at t=10ms.
    InputEvent ev1;
    ev1.set_timestamp_nanos(10'000'000LL);
    ev1.mutable_text()->set_text("1");
    ASSERT_TRUE(writer->Write(ev1));

    // Event 2 at t=30ms (+20ms delta -> sleeps 20ms).
    InputEvent ev2;
    ev2.set_timestamp_nanos(30'000'000LL);
    ev2.mutable_text()->set_text("2");
    ASSERT_TRUE(writer->Write(ev2));

    // Event 3 at t=10,030ms (+10s delta -> capped at max_stream_step_duration = 1s).
    InputEvent ev3;
    ev3.set_timestamp_nanos(10'030'000'000LL);
    ev3.mutable_text()->set_text("3");
    ASSERT_TRUE(writer->Write(ev3));

    ASSERT_TRUE(writer->WritesDone());
    ASSERT_TRUE(writer->Finish().ok());
    EXPECT_EQ(resp.events_dispatched(), 3);

    const auto sleeps = clock_.SleepCalls();
    ASSERT_EQ(sleeps.size(), 2);
    EXPECT_EQ(sleeps[0], absl::Milliseconds(20));
    EXPECT_EQ(sleeps[1], absl::Seconds(1));
}

TEST_F(InputServiceImplTest,
       ConcurrentDispatchesPreserveStrictActivityOrderAndMonotonicTimestamps) {
    auto opts = DefaultOptions();
    opts.now_fn = [this]() {
        clock_.Sleep(absl::Nanoseconds(1));
        return clock_.Now();
    };
    StartServer(std::move(opts));

    auto* activity_writer =
            service_->CreateInputActivityWriter<test::FakeWriteReactor<InputActivity>>(
                    StreamInputActivityRequest());

    constexpr int kNumThreads = 4;
    constexpr int kEventsPerThread = 25;
    std::vector<std::thread> workers;
    workers.reserve(kNumThreads);
    for (int t = 0; t < kNumThreads; ++t) {
        workers.emplace_back([this, t]() {
            const std::string payload(1, static_cast<char>('a' + t));
            for (int i = 0; i < kEventsPerThread; ++i) {
                ::grpc::ServerContext ctx;
                SendInputEventsRequest req;
                auto* ev = req.add_events();
                ev->set_input_source_id(t + 1);
                ev->mutable_text()->set_text(payload);
                SendInputEventsResponse resp;
                EXPECT_TRUE(service_->SendInputEvents(&ctx, &req, &resp).ok());
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    DrainActivity(activity_writer);

    const auto hw_events = key_sender_->Events();
    ASSERT_EQ(hw_events.size(), kNumThreads * kEventsPerThread);
    ASSERT_EQ(activity_writer->written_messages.size(), hw_events.size());
    for (size_t i = 0; i < hw_events.size(); ++i) {
        EXPECT_EQ(activity_writer->written_messages[i].event().text().text(), hw_events[i].text())
                << "Activity order diverged from hardware dispatch order at index " << i;
        if (i > 0) {
            EXPECT_GT(activity_writer->written_messages[i].dispatched_at_nanos(),
                      activity_writer->written_messages[i - 1].dispatched_at_nanos())
                    << "Non-monotonic dispatched_at_nanos at index " << i;
        }
    }
    activity_writer->OnDone();
}

}  // namespace
}  // namespace goldfish::grpc::v2
