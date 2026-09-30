// Copyright 2025 The Android Open Source Project
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
#include "goldfish/display/display_frames.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/display/frame.h"
#include "goldfish/display/geometry.h"

namespace goldfish::display {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::goldfish::async::testing::TestEventLoop;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

constexpr uint8_t kDisplayId = 3;
constexpr Dimensions kConfigured{8, 6};
constexpr Dimensions kSize{4, 4};
constexpr Dimensions kOtherSize{6, 2};

// Records demand changes and whether they arrived on the display loop.
class FakeProducer : public IFrameProducer {
  public:
    explicit FakeProducer(TestEventLoop* loop) : loop_(loop) {}

    void SetDemand(bool warm) override {
        absl::MutexLock lock(&producer_mutex_);
        demand_calls_.push_back(warm);
        all_on_loop_ = all_on_loop_ && loop_->IsOnLoopThread();
    }
    void RequestFrame() override {
        absl::MutexLock lock(&producer_mutex_);
        ++request_calls_;
    }

    std::vector<bool> DemandCalls() const {
        absl::MutexLock lock(&producer_mutex_);
        return demand_calls_;
    }
    int RequestCalls() const {
        absl::MutexLock lock(&producer_mutex_);
        return request_calls_;
    }
    bool HasDemand() const {
        absl::MutexLock lock(&producer_mutex_);
        return !demand_calls_.empty() && demand_calls_.back();
    }
    bool AllOnLoop() const {
        absl::MutexLock lock(&producer_mutex_);
        return all_on_loop_;
    }

  private:
    TestEventLoop* const loop_;
    mutable absl::Mutex producer_mutex_;
    std::vector<bool> demand_calls_ ABSL_GUARDED_BY(producer_mutex_);
    int request_calls_ ABSL_GUARDED_BY(producer_mutex_) = 0;
    bool all_on_loop_ ABSL_GUARDED_BY(producer_mutex_) = true;
};

// Fills every byte of an RGB target with its tag.
class FakeSlot : public Slot {
  public:
    FakeSlot(uint8_t tag, Dimensions size, std::function<void()> on_destroy)
            : tag_(tag), size_(size), on_destroy_(std::move(on_destroy)) {}
    ~FakeSlot() override {
        if (on_destroy_) on_destroy_();
    }

    Dimensions size() const override { return size_; }

  protected:
    absl::Status DoReadInto(const ReadRequest& /*read*/, const ReadTarget& dst) const override {
        if (const auto* nv12 = std::get_if<Nv12Target>(&dst)) {
            std::fill(nv12->y.data.begin(), nv12->y.data.end(), tag_);
            std::fill(nv12->uv.data.begin(), nv12->uv.data.end(), tag_);
            return absl::OkStatus();
        }
        const Plane& plane = std::holds_alternative<Rgba8888Target>(dst)
                                     ? std::get<Rgba8888Target>(dst).plane
                                     : std::get<Rgb888Target>(dst).plane;
        std::fill(plane.data.begin(), plane.data.end(), tag_);
        return absl::OkStatus();
    }

  private:
    const uint8_t tag_;
    const Dimensions size_;
    const std::function<void()> on_destroy_;
};

// Reads the tag of a frame through a whole-frame RGBA read.
absl::StatusOr<uint8_t> ReadTag(const SurfaceFrame& frame) {
    std::vector<uint8_t> pixels(size_t{frame.dimensions().width} * frame.dimensions().height * 4);
    absl::Status status = frame.ReadInto({}, Rgba8888Target{Plane{absl::MakeSpan(pixels)}});
    if (!status.ok()) return status;
    return pixels[0];
}

// Records the events of one listener.
class EventLog {
  public:
    explicit EventLog(TestEventLoop* loop) : loop_(loop) {}

    FrameListener Callback() {
        return [this](const FrameEvent& event) {
            absl::MutexLock lock(&events_mutex_);
            events_.push_back(event);
            all_on_loop_ = all_on_loop_ && loop_->IsOnLoopThread();
        };
    }
    std::vector<FrameEvent> Events() const {
        absl::MutexLock lock(&events_mutex_);
        return events_;
    }
    std::vector<FrameId> Ids() const {
        std::vector<FrameId> ids;
        for (const FrameEvent& event : Events()) ids.push_back(event.frame_id);
        return ids;
    }
    bool AllOnLoop() const {
        absl::MutexLock lock(&events_mutex_);
        return all_on_loop_;
    }

  private:
    TestEventLoop* const loop_;
    mutable absl::Mutex events_mutex_;
    std::vector<FrameEvent> events_ ABSL_GUARDED_BY(events_mutex_);
    bool all_on_loop_ ABSL_GUARDED_BY(events_mutex_) = true;
};

class DisplayFramesTest : public ::testing::Test {
  protected:
    DisplayFramesTest() {
        frames_->SetProducer(producer_);
        loop_->RunAll();
    }
    ~DisplayFramesTest() override { loop_->RunAll(); }

    void Publish(uint8_t tag, Dimensions size = kSize, std::function<void()> on_destroy = {}) {
        frames_->PublishFrame(
                SlotLease(std::make_unique<FakeSlot>(tag, size, std::move(on_destroy))),
                absl::FromUnixSeconds(tag));
    }
    FramePtr Latest() const { return frames_->AcquireLatestFrame().frame; }
    void RemoveOnLoop() {
        ASSERT_TRUE(loop_->Post([this] { frames_->Remove(); }).ok());
        loop_->RunAll();
    }
    // Runs the loop until a waiter on another thread has created demand.
    void RunUntilDemand() {
        while (!producer_->HasDemand()) {
            loop_->RunAll();
            std::this_thread::yield();
        }
    }

    std::unique_ptr<TestEventLoop> loop_ = TestEventLoop::Create();
    std::shared_ptr<DisplayFrames> frames_ =
            DisplayFrames::Create(loop_.get(), kDisplayId, kConfigured);
    std::shared_ptr<FakeProducer> producer_ = std::make_shared<FakeProducer>(loop_.get());
};

TEST_F(DisplayFramesTest, StartsEmptyAndCold) {
    const LatestFrame latest = frames_->AcquireLatestFrame();
    EXPECT_EQ(latest.frame, nullptr);
    EXPECT_FALSE(latest.warm);
    EXPECT_FALSE(latest.removed);
    EXPECT_EQ(frames_->CurrentFrameId(), 0U);
    EXPECT_THAT(producer_->DemandCalls(), IsEmpty());
}

TEST_F(DisplayFramesTest, PublishedFrameBecomesTheLatestFrame) {
    Publish(7);
    const LatestFrame latest = frames_->AcquireLatestFrame();
    ASSERT_NE(latest.frame, nullptr);
    const FrameEvent& event = latest.frame->event();
    EXPECT_EQ(event.frame_id, 1U);
    EXPECT_EQ(event.dimensions, kSize);
    EXPECT_EQ(event.display_id, kDisplayId);
    EXPECT_FALSE(event.blank);
    EXPECT_EQ(event.capture_time, absl::FromUnixSeconds(7));
    EXPECT_THAT(ReadTag(*latest.frame), IsOkAndHolds(7));
    EXPECT_FALSE(latest.warm);  // Nobody asked for frames.
    EXPECT_EQ(frames_->CurrentFrameId(), 1U);
}

TEST_F(DisplayFramesTest, HeldFramesStayReadableAfterANewerPublish) {
    Publish(1);
    const FramePtr old = Latest();
    Publish(2);
    EXPECT_THAT(ReadTag(*old), IsOkAndHolds(1));
    EXPECT_THAT(ReadTag(*Latest()), IsOkAndHolds(2));
}

TEST_F(DisplayFramesTest, ReplacedFrameIsReleasedWithoutTheLock) {
    bool released = false;
    Publish(1, kSize, [&] {
        frames_->AcquireLatestFrame();  // Deadlocks if called under the lock.
        released = true;
    });
    Publish(2);
    EXPECT_TRUE(released);
}

TEST_F(DisplayFramesTest, ListenersGetEventsInOrderOnTheDisplayLoop) {
    EventLog log(loop_.get());
    ScopedFrameListener listener = frames_->AddFrameListener(log.Callback());
    loop_->RunAll();
    Publish(1);
    Publish(2, kOtherSize);
    Publish(3);
    EXPECT_THAT(log.Ids(), IsEmpty());  // Never inline.
    loop_->RunAll();
    EXPECT_THAT(log.Ids(), ElementsAre(1, 2, 3));
    EXPECT_EQ(log.Events()[1].dimensions, kOtherSize);
    EXPECT_TRUE(log.AllOnLoop());
}

TEST_F(DisplayFramesTest, ListenerCreatesDemandAndAPublishCompletesWarmUp) {
    ScopedFrameListener listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    EXPECT_THAT(producer_->DemandCalls(), ElementsAre(true));
    EXPECT_FALSE(frames_->AcquireLatestFrame().warm);
    Publish(1);
    EXPECT_TRUE(frames_->AcquireLatestFrame().warm);
    EXPECT_TRUE(producer_->AllOnLoop());
}

TEST_F(DisplayFramesTest, FrameFromBeforeTheDemandIsNotWarm) {
    Publish(1);
    ScopedFrameListener listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    const LatestFrame latest = frames_->AcquireLatestFrame();
    EXPECT_THAT(ReadTag(*latest.frame), IsOkAndHolds(1));
    EXPECT_FALSE(latest.warm);
}

TEST_F(DisplayFramesTest, ListenerAddedWhileWarmGetsTheCurrentEventAddressedToIt) {
    EventLog first(loop_.get());
    ScopedFrameListener first_listener = frames_->AddFrameListener(first.Callback());
    loop_->RunAll();
    Publish(1);
    loop_->RunAll();

    EventLog second(loop_.get());
    ScopedFrameListener second_listener = frames_->AddFrameListener(second.Callback());
    EXPECT_THAT(second.Ids(), IsEmpty());  // Never inline.
    loop_->RunAll();
    EXPECT_THAT(second.Ids(), ElementsAre(1));
    EXPECT_THAT(first.Ids(), ElementsAre(1));
}

TEST_F(DisplayFramesTest, ListenerAddedWhileWarmingUpGetsTheEventWhenNoContentCompletesWarmUp) {
    frames_->PublishNoContent();  // Frame 1, a blank; cold.
    EventLog log(loop_.get());
    ScopedFrameListener listener = frames_->AddFrameListener(log.Callback());
    loop_->RunAll();
    EXPECT_THAT(log.Ids(), IsEmpty());  // Warming up.

    frames_->PublishNoContent();  // Still no content: completes warm-up, publishes nothing.
    loop_->RunAll();
    EXPECT_THAT(log.Ids(), ElementsAre(1));
    EXPECT_TRUE(frames_->AcquireLatestFrame().warm);
    EXPECT_EQ(frames_->CurrentFrameId(), 1U);
}

TEST_F(DisplayFramesTest, DisplayStaysWarmForTheWarmTimeAfterTheLastListener) {
    ScopedFrameListener listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    Publish(1);
    listener.reset();
    loop_->RunAll();

    loop_->AdvanceClock(DisplayFrames::kWarmTime - absl::Milliseconds(1));
    EXPECT_THAT(producer_->DemandCalls(), ElementsAre(true));
    EXPECT_TRUE(frames_->AcquireLatestFrame().warm);

    loop_->AdvanceClock(absl::Milliseconds(1));
    EXPECT_THAT(producer_->DemandCalls(), ElementsAre(true, false));
    const LatestFrame latest = frames_->AcquireLatestFrame();
    EXPECT_FALSE(latest.warm);
    EXPECT_THAT(ReadTag(*latest.frame), IsOkAndHolds(1));  // Cold keeps the last frame.
}

TEST_F(DisplayFramesTest, DemandReturningWithinTheWarmTimeKeepsCapturing) {
    ScopedFrameListener listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    listener.reset();
    loop_->RunAll();
    loop_->AdvanceClock(absl::Seconds(1));

    listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    loop_->AdvanceClock(absl::Seconds(5));
    EXPECT_THAT(producer_->DemandCalls(), ElementsAre(true));

    listener.reset();
    loop_->RunAll();
    loop_->AdvanceClock(DisplayFrames::kWarmTime);
    EXPECT_THAT(producer_->DemandCalls(), ElementsAre(true, false));
}

TEST_F(DisplayFramesTest, NewProducerIsWarmedUpByExistingDemand) {
    ScopedFrameListener listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    auto replacement = std::make_shared<FakeProducer>(loop_.get());
    frames_->SetProducer(replacement);
    loop_->RunAll();
    EXPECT_THAT(producer_->DemandCalls(), ElementsAre(true, false));
    EXPECT_THAT(replacement->DemandCalls(), ElementsAre(true));
    EXPECT_FALSE(frames_->AcquireLatestFrame().warm);

    // Rebinding the same producer stops it before restarting it.
    frames_->SetProducer(replacement);
    loop_->RunAll();
    EXPECT_THAT(replacement->DemandCalls(), ElementsAre(true, false, true));
}

TEST_F(DisplayFramesTest, ResetDuringCoolDownRequestsAFrameWhenDemandReturns) {
    ScopedFrameListener listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    Publish(1);
    listener.reset();
    loop_->RunAll();  // In cool-down: capturing_ == true, demand_ == 0.

    frames_->OnProducerReset();  // warm_ becomes false while still capturing.
    EXPECT_EQ(producer_->RequestCalls(), 0);

    listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    EXPECT_THAT(producer_->DemandCalls(), ElementsAre(true));
    EXPECT_EQ(producer_->RequestCalls(), 1);
}

TEST_F(DisplayFramesTest, DoesNotOwnItsProducer) {
    ScopedFrameListener listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    const std::weak_ptr<FakeProducer> producer = producer_;
    producer_.reset();
    EXPECT_TRUE(producer.expired());

    // Demand changes and removal without a live producer are no-ops.
    listener.reset();
    loop_->RunAll();
    loop_->AdvanceClock(DisplayFrames::kWarmTime);
    listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    RemoveOnLoop();
    EXPECT_TRUE(frames_->AcquireLatestFrame().removed);
}

TEST_F(DisplayFramesTest, ResetPublishesABlankAtTheLastSizeAndInvalidatesOlderFrames) {
    ScopedFrameListener listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    Publish(1);
    const FramePtr old = Latest();
    frames_->OnProducerReset();

    const LatestFrame latest = frames_->AcquireLatestFrame();
    ASSERT_NE(latest.frame, nullptr);
    EXPECT_EQ(latest.frame->event().frame_id, 2U);
    EXPECT_TRUE(latest.frame->event().blank);
    EXPECT_EQ(latest.frame->dimensions(), kSize);
    EXPECT_FALSE(latest.warm);
    EXPECT_THAT(ReadTag(*old), StatusIs(absl::StatusCode::kUnavailable));
    EXPECT_THAT(ReadTag(*latest.frame), IsOkAndHolds(0));

    Publish(3);
    EXPECT_TRUE(frames_->AcquireLatestFrame().warm);
}

TEST_F(DisplayFramesTest, RepeatedResetPublishesAndInvalidatesNothing) {
    Publish(1);
    frames_->OnProducerReset();
    const FramePtr blank = Latest();
    frames_->OnProducerReset();
    EXPECT_EQ(frames_->CurrentFrameId(), 2U);
    EXPECT_THAT(ReadTag(*blank), IsOkAndHolds(0));
}

TEST_F(DisplayFramesTest, ResetBeforeAnyFramePublishesNothing) {
    frames_->OnProducerReset();
    EXPECT_EQ(Latest(), nullptr);
    EXPECT_EQ(frames_->CurrentFrameId(), 0U);
}

TEST_F(DisplayFramesTest, NoContentPublishesABlankAtTheConfiguredSize) {
    frames_->PublishNoContent();
    const FramePtr blank = Latest();
    ASSERT_NE(blank, nullptr);
    EXPECT_TRUE(blank->event().blank);
    EXPECT_EQ(blank->dimensions(), kConfigured);
    EXPECT_EQ(blank->event().capture_time, absl::InfinitePast());
}

TEST_F(DisplayFramesTest, ConfiguredSizeCanChange) {
    frames_->SetConfiguredSize(kOtherSize);
    frames_->PublishNoContent();
    EXPECT_EQ(Latest()->dimensions(), kOtherSize);
}

TEST_F(DisplayFramesTest, NoContentAfterAResetAddsNoSecondBlankAndCompletesWarmUp) {
    ScopedFrameListener listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    Publish(1);
    frames_->OnProducerReset();
    frames_->PublishNoContent();
    EXPECT_EQ(frames_->CurrentFrameId(), 2U);
    EXPECT_TRUE(frames_->AcquireLatestFrame().warm);
}

TEST_F(DisplayFramesTest, NoContentAfterAnIdempotentResetAddsNoBlank) {
    Publish(1);
    frames_->OnProducerReset();
    frames_->OnProducerReset();
    frames_->PublishNoContent();
    EXPECT_EQ(frames_->CurrentFrameId(), 2U);
}

TEST_F(DisplayFramesTest, NoContentReplacesAFrameWithABlankAndInvalidatesNothing) {
    Publish(1);
    const FramePtr old = Latest();
    frames_->PublishNoContent();
    EXPECT_TRUE(Latest()->event().blank);
    EXPECT_EQ(Latest()->dimensions(), kSize);
    EXPECT_THAT(ReadTag(*old), IsOkAndHolds(1));
}

TEST_F(DisplayFramesTest, BlankFramesReadAsBlackInEveryFormat) {
    frames_->PublishNoContent();
    const FramePtr blank = Latest();
    const ReadRequest request{.output = {2, 2}};

    std::vector<uint8_t> rgba(size_t{2} * 2 * 4);
    ASSERT_TRUE(blank->ReadInto(request, Rgba8888Target{Plane{absl::MakeSpan(rgba)}}).ok());
    EXPECT_THAT(rgba, ElementsAre(0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255));

    std::vector<uint8_t> rgb(size_t{2} * 2 * 3, 0xAA);
    ASSERT_TRUE(blank->ReadInto(request, Rgb888Target{Plane{absl::MakeSpan(rgb)}}).ok());
    EXPECT_THAT(rgb, Each(0));

    std::vector<uint8_t> y(size_t{2} * 2);
    std::vector<uint8_t> uv(2);
    ASSERT_TRUE(blank->ReadInto(request,
                                Nv12Target{Plane{absl::MakeSpan(y)}, Plane{absl::MakeSpan(uv)}})
                        .ok());
    EXPECT_THAT(y, Each(16));
    EXPECT_THAT(uv, Each(128));
}

TEST_F(DisplayFramesTest, ReadIntoRejectsARequestThatDoesNotFitTheFrame) {
    Publish(1);
    std::vector<uint8_t> pixels(64);
    const ReadRequest outside{.crop = Rect{.x = 2, .y = 0, .width = 4, .height = 4}};
    EXPECT_THAT(Latest()->ReadInto(outside, Rgba8888Target{Plane{absl::MakeSpan(pixels)}}),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(DisplayFramesTest, ReadIntoAllocatorReceivesResolvedLayout) {
    Publish(1, kOtherSize);
    std::vector<uint8_t> pixels;
    const absl::StatusOr<Dimensions> dims =
            Latest()->ReadInto(ReadRequest{.rotation = ImageRotation::kRotation90},
                               [&](const ReadLayout& layout) -> absl::StatusOr<Rgb888Target> {
                                   EXPECT_EQ(layout.dimensions, (Dimensions{2, 6}));
                                   EXPECT_EQ(layout.total_bytes, 2U * 6 * 3);
                                   pixels.resize(layout.total_bytes);
                                   return Rgb888Target{.plane = {.data = absl::MakeSpan(pixels)}};
                               });
    EXPECT_THAT(dims, IsOkAndHolds(Dimensions{2, 6}));
}

TEST_F(DisplayFramesTest, WaitForFreshFrameCreatesDemandAndReturnsTheNextFrame) {
    Publish(1);  // Left over from before; not fresh.
    absl::StatusOr<FramePtr> fresh;
    std::thread consumer([&] { fresh = frames_->WaitForFreshFrame(absl::InfiniteFuture()); });
    RunUntilDemand();
    Publish(2);
    consumer.join();
    ASSERT_TRUE(fresh.ok());
    EXPECT_THAT(ReadTag(**fresh), IsOkAndHolds(2));

    loop_->RunAll();  // The demand is gone; the display cools down later.
    loop_->AdvanceClock(DisplayFrames::kWarmTime);
    EXPECT_THAT(producer_->DemandCalls(), ElementsAre(true, false));
}

TEST_F(DisplayFramesTest, WaitForFreshFrameReturnsAtOnceWhenWarm) {
    ScopedFrameListener listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    Publish(1);
    absl::StatusOr<FramePtr> fresh = frames_->WaitForFreshFrame(absl::InfinitePast());
    ASSERT_TRUE(fresh.ok());
    EXPECT_THAT(ReadTag(**fresh), IsOkAndHolds(1));
}

TEST_F(DisplayFramesTest, WaitForFreshFrameTimesOut) {
    Publish(1);
    EXPECT_THAT(frames_->WaitForFreshFrame(absl::Now()),
                StatusIs(absl::StatusCode::kDeadlineExceeded));
}

TEST_F(DisplayFramesTest, WaitForFreshFrameEndsWithNotFoundOnRemoval) {
    absl::StatusOr<FramePtr> fresh;
    std::thread consumer([&] { fresh = frames_->WaitForFreshFrame(absl::InfiniteFuture()); });
    RunUntilDemand();
    RemoveOnLoop();
    consumer.join();
    EXPECT_THAT(fresh, StatusIs(absl::StatusCode::kNotFound));
    EXPECT_THAT(frames_->WaitForFreshFrame(absl::InfiniteFuture()),
                StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(DisplayFramesTest, WaitForFrameReturnsANewerFrameWithoutCreatingDemand) {
    Publish(1);
    absl::StatusOr<FramePtr> first = frames_->WaitForFrame(absl::InfinitePast(), 0);
    ASSERT_TRUE(first.ok());
    EXPECT_EQ((*first)->event().frame_id, 1U);
    EXPECT_THAT(frames_->WaitForFrame(absl::InfinitePast(), 1),
                StatusIs(absl::StatusCode::kDeadlineExceeded));

    absl::StatusOr<FramePtr> next;
    std::thread consumer([&] { next = frames_->WaitForFrame(absl::InfiniteFuture(), 1); });
    Publish(2);
    consumer.join();
    ASSERT_TRUE(next.ok());
    EXPECT_EQ((*next)->event().frame_id, 2U);
    loop_->RunAll();
    EXPECT_THAT(producer_->DemandCalls(), IsEmpty());
}

TEST_F(DisplayFramesTest, WaitForFrameEndsWithNotFoundOnRemoval) {
    RemoveOnLoop();
    EXPECT_THAT(frames_->WaitForFrame(absl::InfiniteFuture(), 0),
                StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(DisplayFramesTest, RemovalEndsWithAFinalEventAndReleasesEverything) {
    EventLog log(loop_.get());
    ScopedFrameListener listener = frames_->AddFrameListener(log.Callback());
    loop_->RunAll();
    Publish(1);
    const FramePtr old = Latest();
    const std::weak_ptr<FakeProducer> producer = producer_;
    RemoveOnLoop();

    ASSERT_THAT(log.Ids(), ElementsAre(1, 2));
    EXPECT_EQ(log.Events()[1].dimensions, (Dimensions{0, 0}));
    const LatestFrame latest = frames_->AcquireLatestFrame();
    EXPECT_EQ(latest.frame, nullptr);
    EXPECT_TRUE(latest.removed);
    EXPECT_FALSE(latest.warm);
    EXPECT_THAT(ReadTag(*old), StatusIs(absl::StatusCode::kUnavailable));
    EXPECT_THAT(producer_->DemandCalls(), ElementsAre(true, false));
    producer_.reset();
    EXPECT_TRUE(producer.expired());  // The display let go of its producer.

    // A removed display emits nothing more.
    bool released = false;
    Publish(3, kSize, [&] { released = true; });
    EXPECT_TRUE(released);
    frames_->OnProducerReset();
    frames_->PublishNoContent();
    loop_->RunAll();
    EXPECT_THAT(log.Ids(), ElementsAre(1, 2));
    EXPECT_EQ(frames_->CurrentFrameId(), 2U);
    listener.reset();  // A no-op now.
    EXPECT_FALSE(frames_->AddFrameListener(log.Callback()));
}

TEST_F(DisplayFramesTest, ListenerMayOutliveTheDisplay) {
    ScopedFrameListener listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    frames_.reset();
    listener.reset();
}

TEST_F(DisplayFramesTest, FramesOutlivingTheDisplayBecomeUnreadable) {
    Publish(1);
    const FramePtr leased = Latest();
    frames_->PublishNoContent();
    const FramePtr blank = Latest();
    frames_.reset();
    EXPECT_THAT(ReadTag(*leased), StatusIs(absl::StatusCode::kUnavailable));
    EXPECT_THAT(ReadTag(*blank), StatusIs(absl::StatusCode::kUnavailable));
}

TEST_F(DisplayFramesTest, ListenerMayUnsubscribeInsideItsOwnCallback) {
    ScopedFrameListener listener;
    int calls = 0;
    listener = frames_->AddFrameListener([&](const FrameEvent&) {
        ++calls;
        listener.reset();
    });
    loop_->RunAll();
    Publish(1);
    Publish(2);
    loop_->RunAll();
    EXPECT_EQ(calls, 1);
}

TEST_F(DisplayFramesTest, UnsubscribingWaitsForARunningCallback) {
    absl::Notification entered;
    absl::Notification release;
    std::atomic<bool> finished{false};
    std::atomic<bool> reset_returned{false};
    ScopedFrameListener listener = frames_->AddFrameListener([&](const FrameEvent&) {
        entered.Notify();
        release.WaitForNotification();
        finished = true;
    });
    loop_->RunAll();
    Publish(1);
    std::thread loop_runner([&] { loop_->RunAll(); });
    entered.WaitForNotification();
    EXPECT_EQ(loop_->TaskCount(), 0U);

    std::thread unsubscriber([&] {
        listener.reset();
        reset_returned = true;
        EXPECT_TRUE(finished);
    });
    // reset() drops the demand, which queues a task behind the busy callback,
    // right before it waits for that callback.
    while (loop_->TaskCount() == 0) std::this_thread::yield();
    absl::SleepFor(absl::Milliseconds(20));
    EXPECT_FALSE(reset_returned);
    release.Notify();
    unsubscriber.join();
    loop_runner.join();
    EXPECT_TRUE(reset_returned);
}

TEST_F(DisplayFramesTest, ListenersMayCallBackIntoTheDisplay) {
    LatestFrame seen;
    ScopedFrameListener inner;
    ScopedFrameListener listener = frames_->AddFrameListener([&](const FrameEvent&) {
        seen = frames_->AcquireLatestFrame();
        if (!inner) inner = frames_->AddFrameListener([](const FrameEvent&) {});
    });
    loop_->RunAll();
    Publish(1);
    loop_->RunAll();
    ASSERT_NE(seen.frame, nullptr);
    EXPECT_EQ(seen.frame->event().frame_id, 1U);
    EXPECT_TRUE(inner);
}

TEST_F(DisplayFramesTest, ConcurrentPublishesArriveInFrameIdOrder) {
    constexpr int kPublishers = 4;
    constexpr int kFramesEach = 200;
    std::vector<FrameId> ids;  // Display loop only.
    ScopedFrameListener listener = frames_->AddFrameListener(
            [&](const FrameEvent& event) { ids.push_back(event.frame_id); });
    loop_->RunAll();

    std::atomic<int> running{kPublishers};
    std::vector<std::thread> publishers;
    for (int p = 0; p < kPublishers; ++p) {
        publishers.emplace_back([&, p] {
            for (int i = 0; i < kFramesEach; ++i) {
                if (i % 50 == 0) frames_->OnProducerReset();
                Publish(static_cast<uint8_t>(p));
            }
            --running;
        });
    }
    while (running > 0) loop_->RunAll();
    for (std::thread& publisher : publishers) publisher.join();
    loop_->RunAll();

    EXPECT_TRUE(std::is_sorted(ids.begin(), ids.end()));
    EXPECT_EQ(std::adjacent_find(ids.begin(), ids.end()), ids.end());
    EXPECT_EQ(ids.size(), frames_->CurrentFrameId());  // Lossless.
}

TEST_F(DisplayFramesTest,
       SetRotationStampsFutureFramesAndReemitsCurrentFrameWithoutIncrementingId) {
    EXPECT_EQ(frames_->GetRotation(), ImageRotation::kRotation0);
    frames_->SetRotation(ImageRotation::kRotation90);
    EXPECT_EQ(frames_->GetRotation(), ImageRotation::kRotation90);
    EXPECT_EQ(frames_->CurrentFrameId(), 0U);

    EventLog log(loop_.get());
    ScopedFrameListener listener = frames_->AddFrameListener(log.Callback());
    loop_->RunAll();

    bool destroyed = false;
    Publish(7, kSize, [&] { destroyed = true; });
    loop_->RunAll();
    ASSERT_EQ(log.Events().size(), 1U);
    EXPECT_EQ(log.Events()[0].frame_id, 1U);
    EXPECT_EQ(log.Events()[0].rotation, ImageRotation::kRotation90);
    FramePtr first = Latest();
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->rotation(), ImageRotation::kRotation90);

    // Same rotation is a no-op.
    frames_->SetRotation(ImageRotation::kRotation90);
    loop_->RunAll();
    EXPECT_EQ(log.Events().size(), 1U);

    // Changing rotation at 0 fps replaces Latest() with updated rotation, keeps
    // frame_id == 1, shares the underlying SlotLease, and notifies listeners.
    frames_->SetRotation(ImageRotation::kRotation270);
    loop_->RunAll();
    ASSERT_EQ(log.Events().size(), 2U);
    EXPECT_EQ(log.Events()[1].frame_id, 1U);
    EXPECT_EQ(log.Events()[1].rotation, ImageRotation::kRotation270);
    EXPECT_EQ(frames_->CurrentFrameId(), 1U);

    FramePtr rotated = Latest();
    ASSERT_NE(rotated, nullptr);
    EXPECT_EQ(rotated->event().frame_id, 1U);
    EXPECT_EQ(rotated->rotation(), ImageRotation::kRotation270);
    EXPECT_THAT(ReadTag(*first), IsOkAndHolds(7));
    EXPECT_THAT(ReadTag(*rotated), IsOkAndHolds(7));

    // Both frames share one lease, released only after the last holder.
    first = nullptr;
    EXPECT_FALSE(destroyed);
    rotated = nullptr;
    EXPECT_FALSE(destroyed);  // The display still holds it as Latest().
    frames_.reset();
    EXPECT_TRUE(destroyed);
}

TEST_F(DisplayFramesTest, SetRotationDeliversToWarmCatchUpSubscriberAndDoesNotWakeWaitForFrame) {
    ScopedFrameListener warm_keeper = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    Publish(1);
    loop_->RunAll();

    // Subscribe while already warm (subscribed_after_ == 1, receives catch-up for frame 1).
    EventLog catch_up_log(loop_.get());
    ScopedFrameListener catch_up_listener = frames_->AddFrameListener(catch_up_log.Callback());
    loop_->RunAll();
    ASSERT_EQ(catch_up_log.Events().size(), 1U);
    EXPECT_EQ(catch_up_log.Events()[0].frame_id, 1U);
    EXPECT_EQ(catch_up_log.Events()[0].rotation, ImageRotation::kRotation0);

    // Rotating at 0 fps still delivers the updated frame 1 event to catch_up_listener,
    // and does NOT satisfy WaitForFrame(..., after=1).
    frames_->SetRotation(ImageRotation::kRotation180);
    loop_->RunAll();
    ASSERT_EQ(catch_up_log.Events().size(), 2U);
    EXPECT_EQ(catch_up_log.Events()[1].frame_id, 1U);
    EXPECT_EQ(catch_up_log.Events()[1].rotation, ImageRotation::kRotation180);
    EXPECT_THAT(frames_->WaitForFrame(absl::InfinitePast(), 1),
                StatusIs(absl::StatusCode::kDeadlineExceeded));
}

TEST_F(DisplayFramesTest, SurfaceFrameReadIntoWithAllocator) {
    Publish(42);
    const FramePtr frame = Latest();
    ASSERT_NE(frame, nullptr);

    std::vector<uint8_t> rgba;
    EXPECT_THAT(frame->ReadInto({},
                                [&](const ReadLayout& layout) -> absl::StatusOr<Rgba8888Target> {
                                    rgba.resize(layout.total_bytes);
                                    return Rgba8888Target{Plane{absl::MakeSpan(rgba)}};
                                }),
                IsOkAndHolds(kSize));
    ASSERT_FALSE(rgba.empty());
    EXPECT_EQ(rgba[0], 42);

    std::vector<uint8_t> rgb;
    EXPECT_THAT(frame->ReadInto({.rotation = ImageRotation::kRotation90}, ReadFormat::kRgb888,
                                [&](const ReadLayout& layout) -> absl::StatusOr<uint8_t*> {
                                    rgb.resize(layout.total_bytes);
                                    return rgb.data();
                                }),
                IsOkAndHolds(Dimensions{kSize.height, kSize.width}));
    EXPECT_EQ(rgb.size(), size_t{kSize.width} * kSize.height * 3);

    std::vector<uint8_t> nv12;
    EXPECT_THAT(frame->ReadInto({.output = {3, 3}},
                                [&](const ReadLayout& layout) -> absl::StatusOr<Nv12Target> {
                                    EXPECT_EQ(layout.dimensions, (Dimensions{2, 2}));
                                    nv12.resize(layout.total_bytes);
                                    return std::get<Nv12Target>(layout.TargetFor(nv12.data()));
                                }),
                IsOkAndHolds(Dimensions{2, 2}));
    EXPECT_THAT(nv12, Each(42));
}

TEST_F(DisplayFramesTest, SurfaceFrameReadIntoAllocatorEdgeCases) {
    Publish(42);
    const FramePtr frame = Latest();
    ASSERT_NE(frame, nullptr);

    // Invalid request rejects before invoking either allocator overload.
    bool allocator_called = false;
    const ReadRequest invalid_crop{.crop = Rect{.x = 10, .y = 0, .width = 2, .height = 2}};
    EXPECT_THAT(frame->ReadInto(invalid_crop,
                                [&](const ReadLayout&) -> absl::StatusOr<Rgba8888Target> {
                                    allocator_called = true;
                                    return Rgba8888Target{};
                                }),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_FALSE(allocator_called);

    EXPECT_THAT(frame->ReadInto(invalid_crop, ReadFormat::kRgba8888,
                                [&](const ReadLayout&) -> absl::StatusOr<uint8_t*> {
                                    allocator_called = true;
                                    return nullptr;
                                }),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_FALSE(allocator_called);

    // Non-OK status from allocator is propagated directly.
    EXPECT_THAT(frame->ReadInto({},
                                [](const ReadLayout&) -> absl::StatusOr<Rgba8888Target> {
                                    return absl::ResourceExhaustedError("out of buffers");
                                }),
                StatusIs(absl::StatusCode::kResourceExhausted));
    EXPECT_THAT(frame->ReadInto({}, ReadFormat::kRgba8888,
                                [](const ReadLayout&) -> absl::StatusOr<uint8_t*> {
                                    return absl::ResourceExhaustedError("out of buffers");
                                }),
                StatusIs(absl::StatusCode::kResourceExhausted));

    // Null pointer from raw-buffer allocator returns INVALID_ARGUMENT.
    EXPECT_THAT(
            frame->ReadInto({}, ReadFormat::kRgba8888,
                            [](const ReadLayout&) -> absl::StatusOr<uint8_t*> { return nullptr; }),
            StatusIs(absl::StatusCode::kInvalidArgument));

    // Target with undersized plane buffer returns INVALID_ARGUMENT.
    std::vector<uint8_t> tiny(1);
    EXPECT_THAT(frame->ReadInto({},
                                [&](const ReadLayout&) -> absl::StatusOr<Rgba8888Target> {
                                    return Rgba8888Target{Plane{absl::MakeSpan(tiny)}};
                                }),
                StatusIs(absl::StatusCode::kInvalidArgument));

    // After a reset, ReadInto with an allocator returns UNAVAILABLE without
    // invoking the allocator, for both valid and invalid requests.
    frames_->OnProducerReset();
    EXPECT_THAT(frame->ReadInto({},
                                [&](const ReadLayout&) -> absl::StatusOr<Rgba8888Target> {
                                    allocator_called = true;
                                    return Rgba8888Target{};
                                }),
                StatusIs(absl::StatusCode::kUnavailable));
    EXPECT_FALSE(allocator_called);

    EXPECT_THAT(frame->ReadInto(invalid_crop, ReadFormat::kRgba8888,
                                [&](const ReadLayout&) -> absl::StatusOr<uint8_t*> {
                                    allocator_called = true;
                                    return nullptr;
                                }),
                StatusIs(absl::StatusCode::kUnavailable));
    EXPECT_FALSE(allocator_called);
}

TEST_F(DisplayFramesTest, WaitForFreshFrameWhileListenerActiveAfterResetRequestsFrame) {
    ScopedFrameListener listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    Publish(1);
    ASSERT_TRUE(frames_->AcquireLatestFrame().warm);

    // Reset while a listener is already holding demand > 0.
    frames_->OnProducerReset();
    EXPECT_FALSE(frames_->AcquireLatestFrame().warm);
    EXPECT_EQ(producer_->RequestCalls(), 0);

    // WaitForFreshFrame increments demand from 1 -> 2 and still posts UpdateDemand()
    // so needs_frame_on_demand_ triggers producer_->RequestFrame().
    absl::StatusOr<FramePtr> fresh;
    std::thread waiter([&] { fresh = frames_->WaitForFreshFrame(absl::InfiniteFuture()); });
    while (producer_->RequestCalls() == 0) {
        loop_->RunAll();
        std::this_thread::yield();
    }
    EXPECT_EQ(producer_->RequestCalls(), 1);

    Publish(9);
    waiter.join();
    ASSERT_TRUE(fresh.ok());
    EXPECT_THAT(ReadTag(**fresh), IsOkAndHolds(9));
}

TEST_F(DisplayFramesTest, BlockedWaitForFrameWakesWithNotFoundOnRemoval) {
    Publish(1);
    std::atomic<bool> returned{false};
    absl::StatusOr<FramePtr> next;
    std::thread consumer([&] {
        next = frames_->WaitForFrame(absl::InfiniteFuture(), /*after=*/1);
        returned = true;
    });
    // WaitForFrame creates no demand, so nothing signals that it is blocked.
    // Give it time to block and check that it is still waiting, so only
    // Remove() can end the wait.
    absl::SleepFor(absl::Milliseconds(20));
    EXPECT_FALSE(returned);
    RemoveOnLoop();
    consumer.join();
    EXPECT_THAT(next, StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(DisplayFramesTest, ListenerDoesNotReceiveQueuedPreSubscriptionEventsWhenCold) {
    // Publish frame 1 while cold; its event is queued on loop_ but not yet dispatched.
    Publish(1);

    EventLog log(loop_.get());
    ScopedFrameListener listener = frames_->AddFrameListener(log.Callback());
    loop_->RunAll();
    // The display was not warm when `listener` subscribed, so frame 1 is filtered
    // by `subscribed_after_`.
    EXPECT_THAT(log.Ids(), IsEmpty());

    Publish(2);
    loop_->RunAll();
    EXPECT_THAT(log.Ids(), ElementsAre(2));
}

TEST_F(DisplayFramesTest, ResetAfterNoContentClearsWarmthWithoutPublishingSecondBlank) {
    ScopedFrameListener listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    frames_->PublishNoContent();
    const FramePtr blank = Latest();
    ASSERT_NE(blank, nullptr);
    EXPECT_EQ(frames_->CurrentFrameId(), 1U);
    EXPECT_TRUE(frames_->AcquireLatestFrame().warm);

    frames_->OnProducerReset();
    EXPECT_FALSE(frames_->AcquireLatestFrame().warm);
    EXPECT_EQ(frames_->CurrentFrameId(), 1U);
    EXPECT_THAT(ReadTag(*blank), IsOkAndHolds(0));
}

TEST_F(DisplayFramesTest, RemovalIsIdempotentAndIgnoresPostRemovalMutations) {
    EventLog log(loop_.get());
    ScopedFrameListener listener = frames_->AddFrameListener(log.Callback());
    loop_->RunAll();
    RemoveOnLoop();
    const FrameId final_id = frames_->CurrentFrameId();
    const size_t event_count = log.Events().size();

    // Second Remove() is a no-op.
    RemoveOnLoop();
    EXPECT_EQ(frames_->CurrentFrameId(), final_id);
    EXPECT_EQ(log.Events().size(), event_count);

    // Post-removal SetRotation and SetProducer are no-ops.
    frames_->SetRotation(ImageRotation::kRotation180);
    EXPECT_EQ(frames_->GetRotation(), ImageRotation::kRotation0);
    auto replacement = std::make_shared<FakeProducer>(loop_.get());
    frames_->SetProducer(replacement);
    loop_->RunAll();
    EXPECT_THAT(replacement->DemandCalls(), IsEmpty());
}

TEST_F(DisplayFramesTest, FrameEventComparisonAndGeometryChangeHelpers) {
    const FrameEvent a{.frame_id = 1, .dimensions = {4, 4}, .rotation = ImageRotation::kRotation0};
    const FrameEvent b{.frame_id = 1, .dimensions = {4, 4}, .rotation = ImageRotation::kRotation0};
    const FrameEvent resized{
        .frame_id = 2, .dimensions = {8, 4}, .rotation = ImageRotation::kRotation0};
    const FrameEvent rotated{
        .frame_id = 2, .dimensions = {4, 4}, .rotation = ImageRotation::kRotation90};

    EXPECT_EQ(a, b);
    EXPECT_LT(a, resized);
    EXPECT_FALSE(a.HasGeometryChangeFrom(b));
    EXPECT_TRUE(a.HasGeometryChangeFrom(resized));
    EXPECT_TRUE(a.HasGeometryChangeFrom(rotated));
}

TEST_F(DisplayFramesTest, SetProducerWhileCoolingDownStopsOldAndDoesNotStartNew) {
    ScopedFrameListener listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    Publish(1);
    listener.reset();
    loop_->RunAll();  // Cooling down: still capturing, no demand.

    auto replacement = std::make_shared<FakeProducer>(loop_.get());
    frames_->SetProducer(replacement);
    loop_->RunAll();
    EXPECT_THAT(producer_->DemandCalls(), ElementsAre(true, false));
    EXPECT_THAT(replacement->DemandCalls(), IsEmpty());

    // The pending cool-down timer stops nobody.
    loop_->AdvanceClock(DisplayFrames::kWarmTime);
    EXPECT_THAT(producer_->DemandCalls(), ElementsAre(true, false));
    EXPECT_THAT(replacement->DemandCalls(), IsEmpty());

    listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    EXPECT_THAT(replacement->DemandCalls(), ElementsAre(true));
    EXPECT_TRUE(replacement->AllOnLoop());
}

TEST_F(DisplayFramesTest, RemoveDuringCoolDownIgnoresTimer) {
    ScopedFrameListener listener = frames_->AddFrameListener([](const FrameEvent&) {});
    loop_->RunAll();
    Publish(1);
    listener.reset();
    loop_->RunAll();  // Cooling down.

    RemoveOnLoop();
    EXPECT_THAT(producer_->DemandCalls(), ElementsAre(true, false));
    loop_->AdvanceClock(DisplayFrames::kWarmTime);
    EXPECT_THAT(producer_->DemandCalls(), ElementsAre(true, false));
    EXPECT_TRUE(frames_->AcquireLatestFrame().removed);
}

TEST_F(DisplayFramesTest, WaitForFreshFrameDeadlineWhileBlockedReleasesDemand) {
    Publish(1);  // Not fresh.
    std::atomic<bool> done{false};
    absl::StatusOr<FramePtr> fresh;
    std::thread consumer([&] {
        fresh = frames_->WaitForFreshFrame(absl::Now() + absl::Milliseconds(100));
        done = true;
    });
    while (!producer_->HasDemand() && !done) {
        loop_->RunAll();
        std::this_thread::yield();
    }
    EXPECT_TRUE(producer_->HasDemand()) << "The waiter timed out before creating demand";
    consumer.join();
    EXPECT_THAT(fresh, StatusIs(absl::StatusCode::kDeadlineExceeded));

    // The expired waiter no longer holds demand: the display cools down.
    loop_->RunAll();
    loop_->AdvanceClock(DisplayFrames::kWarmTime - absl::Milliseconds(1));
    EXPECT_THAT(producer_->DemandCalls(), ElementsAre(true));
    loop_->AdvanceClock(absl::Milliseconds(1));
    EXPECT_THAT(producer_->DemandCalls(), ElementsAre(true, false));
}

TEST_F(DisplayFramesTest, RotationDuringWarmUpNotDeliveredToColdSubscriber) {
    Publish(1);  // Cold: nobody asked for it.
    EventLog log(loop_.get());
    ScopedFrameListener listener = frames_->AddFrameListener(log.Callback());
    loop_->RunAll();
    ASSERT_FALSE(frames_->AcquireLatestFrame().warm);  // Warming up.

    frames_->SetRotation(ImageRotation::kRotation90);  // Re-emits frame 1.
    loop_->RunAll();
    EXPECT_THAT(log.Ids(), IsEmpty());

    Publish(2);  // Completes warm-up.
    loop_->RunAll();
    ASSERT_THAT(log.Ids(), ElementsAre(2));
    EXPECT_EQ(log.Events()[0].rotation, ImageRotation::kRotation90);
}

TEST_F(DisplayFramesTest, ManyThreadsUnsubscribeWhileDispatching) {
    constexpr int kThreads = 6;
    constexpr int kRounds = 200;
    struct Subscription {
        std::atomic<int> calls{0};
        std::atomic<bool> unsubscribed{false};
    };
    std::atomic<int> late_calls{0};
    std::atomic<bool> stop{false};
    absl::Notification start;

    std::thread driver([&] {
        start.WaitForNotification();
        while (!stop) {
            loop_->RunAll();
            std::this_thread::yield();
        }
    });
    std::thread publisher([&] {
        start.WaitForNotification();
        for (uint8_t tag = 0; !stop; ++tag) {
            Publish(tag);
            absl::SleepFor(absl::Microseconds(50));
        }
    });
    std::vector<std::thread> unsubscribers;
    for (int t = 0; t < kThreads; ++t) {
        unsubscribers.emplace_back([&, t] {
            start.WaitForNotification();
            for (int round = 0; round < kRounds; ++round) {
                auto sub = std::make_unique<Subscription>();
                ScopedFrameListener listener =
                        frames_->AddFrameListener([&late_calls, s = sub.get()](const FrameEvent&) {
                            if (s->unsubscribed) ++late_calls;
                            ++s->calls;
                        });
                // Half the rounds unsubscribe mid-stream, half at once.
                if ((round + t) % 2 == 0) {
                    while (sub->calls == 0) std::this_thread::yield();
                }
                listener.reset();
                sub->unsubscribed = true;
                sub.reset();  // A late callback would now be a use-after-free.
            }
        });
    }
    start.Notify();
    for (std::thread& unsubscriber : unsubscribers) unsubscriber.join();
    stop = true;
    publisher.join();
    driver.join();
    loop_->RunAll();

    EXPECT_EQ(late_calls, 0);
    EXPECT_TRUE(producer_->AllOnLoop());
}

TEST_F(DisplayFramesTest, StressPublishSubscribeReadRotateReset) {
    constexpr int kPublishers = 2;
    constexpr int kFramesEach = 3000;
    constexpr int kSubscribers = 3;
    constexpr int kReaders = 2;
    constexpr ImageRotation kRotations[] = {ImageRotation::kRotation0, ImageRotation::kRotation90,
                                            ImageRotation::kRotation180,
                                            ImageRotation::kRotation270};

    // Shared with its callback, so a callback that unsubscribes itself never
    // outlives it.
    struct Subscription {
        bool self_unsubscribe = false;
        absl::Mutex mutex;
        ScopedFrameListener handle ABSL_GUARDED_BY(mutex);
        std::atomic<bool> unsubscribed{false};
        std::atomic<int> calls{0};
        // Display loop only.
        FrameId last_id = 0;
        std::optional<ImageRotation> last_rotation;

        // Never reset the handle under `mutex`: the callback takes it too.
        ScopedFrameListener Take() {
            absl::MutexLock lock(&mutex);
            return std::move(handle);
        }
    };

    std::atomic<int> publishers_running{kPublishers};
    std::atomic<int> order_violations{0};
    std::atomic<int> late_calls{0};
    std::atomic<int> bad_reads{0};
    std::atomic<int> deliveries{0};
    std::atomic<bool> stop{false};
    absl::Notification start;

    auto check_read = [&](const SurfaceFrame& frame) {
        const absl::StatusOr<uint8_t> tag = ReadTag(frame);
        if (tag.ok() ? *tag > kPublishers : tag.status().code() != absl::StatusCode::kUnavailable) {
            ++bad_reads;
        }
    };

    std::thread driver([&] {
        start.WaitForNotification();
        for (int n = 1; !stop; ++n) {
            loop_->RunAll();
            if (n % 64 == 0) loop_->AdvanceClock(DisplayFrames::kWarmTime);  // Cool down.
            std::this_thread::yield();
        }
    });

    std::vector<std::thread> threads;
    for (int p = 0; p < kPublishers; ++p) {
        threads.emplace_back([&, p] {
            start.WaitForNotification();
            for (int i = 0; i < kFramesEach; ++i) {
                Publish(static_cast<uint8_t>(p + 1));
                if (i % 97 == 0) frames_->OnProducerReset();
                if (i % 31 == 0) frames_->PublishNoContent();
                if (i % 23 == 0) frames_->SetRotation(kRotations[(i / 23 + p) % 4]);
                std::this_thread::yield();
            }
            --publishers_running;
        });
    }
    for (int s = 0; s < kSubscribers; ++s) {
        threads.emplace_back([&, s] {
            start.WaitForNotification();
            for (int round = 0; round == 0 || publishers_running > 0; ++round) {
                auto sub = std::make_shared<Subscription>();
                sub->self_unsubscribe = (round + s) % 3 == 0;
                ScopedFrameListener handle =
                        frames_->AddFrameListener([&, sub](const FrameEvent& event) {
                            if (sub->unsubscribed) ++late_calls;
                            if (event.frame_id < sub->last_id ||
                                (event.frame_id == sub->last_id &&
                                 event.rotation == sub->last_rotation)) {
                                ++order_violations;
                            }
                            sub->last_id = event.frame_id;
                            sub->last_rotation = event.rotation;
                            ++sub->calls;
                            ++deliveries;
                            if (!sub->self_unsubscribe) return;
                            // Empty if the subscriber has not stored it yet.
                            if (ScopedFrameListener self = sub->Take()) {
                                self.reset();
                                sub->unsubscribed = true;
                            }
                        });
                {
                    absl::MutexLock lock(&sub->mutex);
                    sub->handle = std::move(handle);
                }
                while (sub->calls == 0 && publishers_running > 0) std::this_thread::yield();
                sub->Take().reset();
                sub->unsubscribed = true;
            }
        });
    }
    for (int r = 0; r < kReaders; ++r) {
        threads.emplace_back([&] {
            start.WaitForNotification();
            do {
                if (const FramePtr latest = Latest()) check_read(*latest);
                const absl::StatusOr<FramePtr> fresh =
                        frames_->WaitForFreshFrame(absl::Now() + absl::Milliseconds(2));
                if (fresh.ok()) {
                    check_read(**fresh);
                } else if (fresh.status().code() != absl::StatusCode::kDeadlineExceeded) {
                    ++bad_reads;
                }
            } while (publishers_running > 0);
        });
    }

    start.Notify();
    for (std::thread& thread : threads) thread.join();
    stop = true;
    driver.join();
    loop_->RunAll();

    EXPECT_EQ(order_violations, 0);
    EXPECT_EQ(late_calls, 0);
    EXPECT_EQ(bad_reads, 0);
    EXPECT_GT(deliveries, 0);
    EXPECT_TRUE(producer_->AllOnLoop());
    const std::vector<bool> demand = producer_->DemandCalls();
    for (size_t i = 0; i < demand.size(); ++i) {
        EXPECT_EQ(demand[i], i % 2 == 0) << "SetDemand call " << i << " does not alternate";
    }
}

}  // namespace
}  // namespace goldfish::display
