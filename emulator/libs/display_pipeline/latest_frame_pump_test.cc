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

#include "goldfish/display/latest_frame_pump.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/display/display_frames.h"
#include "goldfish/display/frame.h"
#include "goldfish/display/geometry.h"

namespace goldfish::display {
namespace {

using ::goldfish::async::testing::TestEventLoop;
using ::testing::ElementsAre;

class DummySlot : public Slot {
  public:
    explicit DummySlot(Dimensions dims) : dims_(dims) {}

    Dimensions size() const override { return dims_; }

  protected:
    absl::Status DoReadInto(const ReadRequest& /*read*/, const ReadTarget& /*dst*/) const override {
        return absl::OkStatus();
    }

  private:
    Dimensions dims_;
};

class StubDisplay {
  public:
    StubDisplay(async::EventLoop* loop, uint8_t id, uint32_t width, uint32_t height)
            : frames_(DisplayFrames::Create(loop, id, {width, height})) {}

    DisplayFrames* Frames() const { return frames_.get(); }

    LatestFrame AcquireLatestFrame() const { return frames_->AcquireLatestFrame(); }

    void Remove() { frames_->Remove(); }

    void PublishSolid(Dimensions dims = {100, 100}) {
        frames_->PublishFrame(SlotLease(std::make_unique<DummySlot>(dims)),
                              absl::FromUnixMillis(1000));
    }

  private:
    std::shared_ptr<DisplayFrames> frames_;
};

class LatestFramePumpTest : public ::testing::Test {
  protected:
    ~LatestFramePumpTest() override {
        display_loop_->RunAll();
        worker_loop_->RunAll();
    }

    std::unique_ptr<TestEventLoop> display_loop_ = TestEventLoop::Create();
    std::unique_ptr<TestEventLoop> worker_loop_ = TestEventLoop::Create();
};

TEST_F(LatestFramePumpTest, ProcessesPublishedFrameOnWorker) {
    auto display = std::make_shared<StubDisplay>(display_loop_.get(), 0, 100, 100);
    display->PublishSolid();

    std::vector<FrameId> processed_ids;
    bool finished = false;
    auto pump = std::make_shared<LatestFramePump>(
            [display] { return display; }, worker_loop_.get(),
            [&](FramePtr frame) { processed_ids.push_back(frame->event().frame_id); },
            [&] { finished = true; });

    pump->OnEvent(FrameEvent{.frame_id = 1});
    EXPECT_TRUE(processed_ids.empty());

    worker_loop_->RunAll();
    ASSERT_EQ(processed_ids.size(), 1U);
    EXPECT_EQ(processed_ids[0], 1U);
    EXPECT_FALSE(finished);
}

TEST_F(LatestFramePumpTest, CoalescesMultipleEventsBeforeWorkerRuns) {
    auto display = std::make_shared<StubDisplay>(display_loop_.get(), 0, 100, 100);
    display->PublishSolid();
    display->PublishSolid();
    display->PublishSolid();

    std::vector<FrameId> processed_ids;
    auto pump = std::make_shared<LatestFramePump>(
            [display] { return display; }, worker_loop_.get(),
            [&](FramePtr frame) { processed_ids.push_back(frame->event().frame_id); });

    pump->OnEvent(FrameEvent{.frame_id = 1});
    pump->OnEvent(FrameEvent{.frame_id = 2});
    pump->OnEvent(FrameEvent{.frame_id = 3});

    worker_loop_->RunAll();
    ASSERT_EQ(processed_ids.size(), 1U);
    EXPECT_EQ(processed_ids[0], 3U);
}

TEST_F(LatestFramePumpTest, RerunsWhenEventArrivesDuringProcess) {
    auto display = std::make_shared<StubDisplay>(display_loop_.get(), 0, 100, 100);
    display->PublishSolid();

    std::vector<FrameId> processed_ids;
    std::shared_ptr<LatestFramePump> pump;
    pump = std::make_shared<LatestFramePump>([display] { return display; }, worker_loop_.get(),
                                             [&](FramePtr frame) {
                                                 processed_ids.push_back(frame->event().frame_id);
                                                 if (processed_ids.size() == 1U) {
                                                     display->PublishSolid();
                                                     pump->OnEvent(FrameEvent{.frame_id = 2});
                                                 }
                                             });

    pump->OnEvent(FrameEvent{.frame_id = 1});
    worker_loop_->RunAll();

    ASSERT_EQ(processed_ids.size(), 2U);
    EXPECT_EQ(processed_ids[0], 1U);
    EXPECT_EQ(processed_ids[1], 2U);
}

TEST_F(LatestFramePumpTest, SkipsNullDisplayAndUnpublishedFrameThenRecovers) {
    std::shared_ptr<StubDisplay> active_display = nullptr;
    std::vector<FrameId> processed_ids;
    auto pump = std::make_shared<LatestFramePump>(
            [&] { return active_display; }, worker_loop_.get(),
            [&](FramePtr frame) { processed_ids.push_back(frame->event().frame_id); });

    // Null display is skipped.
    pump->OnEvent(FrameEvent{});
    worker_loop_->RunAll();
    EXPECT_TRUE(processed_ids.empty());

    // Display with no frame published yet (!latest.frame) is skipped.
    active_display = std::make_shared<StubDisplay>(display_loop_.get(), 0, 100, 100);
    pump->OnEvent(FrameEvent{});
    worker_loop_->RunAll();
    EXPECT_TRUE(processed_ids.empty());

    // First published frame is processed.
    active_display->PublishSolid();
    pump->OnEvent(FrameEvent{.frame_id = 1});
    worker_loop_->RunAll();
    ASSERT_EQ(processed_ids.size(), 1U);
    EXPECT_EQ(processed_ids[0], 1U);
}

TEST_F(LatestFramePumpTest, ProcessesBlankFramesNormally) {
    auto display = std::make_shared<StubDisplay>(display_loop_.get(), 0, 100, 100);
    display->Frames()->PublishNoContent();

    std::vector<FrameEvent> processed_events;
    auto pump = std::make_shared<LatestFramePump>(
            [display] { return display; }, worker_loop_.get(),
            [&](FramePtr frame) { processed_events.push_back(frame->event()); });

    pump->OnEvent(FrameEvent{.frame_id = 1, .blank = true});
    worker_loop_->RunAll();

    ASSERT_EQ(processed_events.size(), 1U);
    EXPECT_EQ(processed_events[0].frame_id, 1U);
    EXPECT_TRUE(processed_events[0].blank);
}

TEST_F(LatestFramePumpTest, SkipsSameFrameIdUnlessRotationChanges) {
    auto display = std::make_shared<StubDisplay>(display_loop_.get(), 0, 100, 100);
    display->PublishSolid();

    std::vector<FrameEvent> processed_events;
    auto pump = std::make_shared<LatestFramePump>(
            [display] { return display; }, worker_loop_.get(),
            [&](FramePtr frame) { processed_events.push_back(frame->event()); });

    pump->OnEvent(FrameEvent{.frame_id = 1});
    worker_loop_->RunAll();
    ASSERT_EQ(processed_events.size(), 1U);
    EXPECT_EQ(processed_events[0].frame_id, 1U);
    EXPECT_EQ(processed_events[0].rotation, ImageRotation::kRotation0);

    // Duplicate OnEvent without a new frame or rotation change is filtered out.
    pump->OnEvent(FrameEvent{.frame_id = 1});
    worker_loop_->RunAll();
    EXPECT_EQ(processed_events.size(), 1U);

    // Updating rotation on DisplayFrames and notifying OnEvent re-emits frame 1 with the new
    // rotation.
    display->Frames()->SetRotation(ImageRotation::kRotation90);
    pump->OnEvent(FrameEvent{.frame_id = 1, .rotation = ImageRotation::kRotation90});
    worker_loop_->RunAll();
    ASSERT_EQ(processed_events.size(), 2U);
    EXPECT_EQ(processed_events[1].frame_id, 1U);
    EXPECT_EQ(processed_events[1].rotation, ImageRotation::kRotation90);

    // Subsequent duplicate OnEvent at the same rotation is filtered again.
    pump->OnEvent(FrameEvent{.frame_id = 1, .rotation = ImageRotation::kRotation90});
    worker_loop_->RunAll();
    EXPECT_EQ(processed_events.size(), 2U);
}

TEST_F(LatestFramePumpTest, DisplayRecreatedAfterTheOldOneIsDestroyedIsProcessed) {
    auto display_a = std::make_shared<StubDisplay>(display_loop_.get(), 1, 100, 100);
    display_a->PublishSolid();
    display_a->PublishSolid();
    display_a->PublishSolid();  // frame_id = 3

    std::shared_ptr<StubDisplay> current_display = display_a;
    std::vector<FrameId> processed_ids;
    auto pump = std::make_shared<LatestFramePump>(
            [&] { return current_display; }, worker_loop_.get(),
            [&](FramePtr frame) { processed_ids.push_back(frame->event().frame_id); });

    pump->OnEvent(FrameEvent{.frame_id = 3});
    worker_loop_->RunAll();
    ASSERT_EQ(processed_ids.size(), 1U);
    EXPECT_EQ(processed_ids[0], 3U);

    // Drop every test reference to A before allocating B, so B could reuse A's
    // address if the pump compared raw pointers.
    current_display.reset();
    display_a.reset();

    // Recreate display with the SAME display_id (1), restarting its frame_id at 1.
    auto display_b = std::make_shared<StubDisplay>(display_loop_.get(), 1, 100, 100);
    display_b->PublishSolid();  // frame_id = 1 < 3
    current_display = display_b;

    pump->OnEvent(FrameEvent{.frame_id = 1});
    worker_loop_->RunAll();
    EXPECT_THAT(processed_ids, ElementsAre(3, 1));
}

TEST_F(LatestFramePumpTest, HingeBackAndForthReprocessesSameFrame) {
    auto display_a = std::make_shared<StubDisplay>(display_loop_.get(), 0, 100, 100);
    auto display_b = std::make_shared<StubDisplay>(display_loop_.get(), 1, 100, 100);
    display_a->PublishSolid();
    display_b->PublishSolid();

    std::shared_ptr<StubDisplay> current_display = display_a;
    std::vector<FramePtr> processed;
    auto pump =
            std::make_shared<LatestFramePump>([&] { return current_display; }, worker_loop_.get(),
                                              [&](FramePtr frame) { processed.push_back(frame); });

    for (const std::shared_ptr<StubDisplay>& display : {display_a, display_b, display_a}) {
        current_display = display;
        pump->OnEvent(FrameEvent{.frame_id = 1});
        worker_loop_->RunAll();
    }

    ASSERT_EQ(processed.size(), 3U);
    EXPECT_EQ(processed[0]->event().display_id, 0);
    EXPECT_EQ(processed[1]->event().display_id, 1);
    // Switching back re-delivers A's unchanged frame.
    EXPECT_EQ(processed[2], processed[0]);
}

TEST_F(LatestFramePumpTest, CallsFinishWhenDisplayIsRemoved) {
    auto display = std::make_shared<StubDisplay>(display_loop_.get(), 0, 100, 100);
    display->PublishSolid();
    ASSERT_TRUE(display_loop_->Post([display] { display->Frames()->Remove(); }).ok());
    display_loop_->RunAll();

    std::vector<FrameId> processed_ids;
    int finish_calls = 0;
    auto pump = std::make_shared<LatestFramePump>(
            [display] { return display; }, worker_loop_.get(),
            [&](FramePtr frame) { processed_ids.push_back(frame->event().frame_id); },
            [&] { ++finish_calls; });

    pump->OnEvent(FrameEvent{.frame_id = 1});
    worker_loop_->RunAll();

    EXPECT_TRUE(processed_ids.empty());
    EXPECT_EQ(finish_calls, 1);
}

TEST_F(LatestFramePumpTest, WeakPtrExpiresCleanlyIfPumpDestroyedBeforeWorkerDrains) {
    auto display = std::make_shared<StubDisplay>(display_loop_.get(), 0, 100, 100);
    display->PublishSolid();

    bool processed = false;
    {
        auto pump =
                std::make_shared<LatestFramePump>([display] { return display; }, worker_loop_.get(),
                                                  [&](FramePtr /*frame*/) { processed = true; });
        pump->OnEvent(FrameEvent{.frame_id = 1});
    }

    worker_loop_->RunAll();
    EXPECT_FALSE(processed);
}

TEST_F(LatestFramePumpTest, FinishCalledOnceAcrossRepeatedEvents) {
    auto display = std::make_shared<StubDisplay>(display_loop_.get(), 0, 100, 100);
    display->PublishSolid();
    ASSERT_TRUE(display_loop_->Post([display] { display->Remove(); }).ok());
    display_loop_->RunAll();

    int process_calls = 0;
    int finish_calls = 0;
    std::shared_ptr<LatestFramePump> pump;
    pump = std::make_shared<LatestFramePump>(
            [display] { return display; }, worker_loop_.get(),
            [&](FramePtr /*frame*/) { ++process_calls; },
            [&] {
                ++finish_calls;
                pump->OnEvent(FrameEvent{});  // Ignored, even from inside `finish`.
            });

    for (int i = 0; i < 3; ++i) {
        pump->OnEvent(FrameEvent{});
        worker_loop_->RunAll();
    }
    EXPECT_EQ(finish_calls, 1);
    EXPECT_EQ(process_calls, 0);
}

TEST_F(LatestFramePumpTest, EventDuringRemovalResolveIsNotLost) {
    auto removed = std::make_shared<StubDisplay>(display_loop_.get(), 0, 100, 100);
    removed->PublishSolid();
    ASSERT_TRUE(display_loop_->Post([removed] { removed->Remove(); }).ok());
    display_loop_->RunAll();
    auto replacement = std::make_shared<StubDisplay>(display_loop_.get(), 1, 100, 100);
    replacement->PublishSolid();

    std::shared_ptr<StubDisplay> current_display = removed;
    std::vector<uint8_t> processed_display_ids;
    int finish_calls = 0;
    std::shared_ptr<LatestFramePump> pump;
    pump = std::make_shared<LatestFramePump>(
            [&] {
                std::shared_ptr<StubDisplay> resolved = current_display;
                if (resolved == removed) {
                    // The active display switches while the removed one is
                    // being resolved, and the replacement's listener fires.
                    current_display = replacement;
                    pump->OnEvent(FrameEvent{.display_id = 1});
                }
                return resolved;
            },
            worker_loop_.get(),
            [&](FramePtr frame) { processed_display_ids.push_back(frame->event().display_id); },
            [&] { ++finish_calls; });

    pump->OnEvent(FrameEvent{.display_id = 0});
    worker_loop_->RunAll();
    EXPECT_THAT(processed_display_ids, ElementsAre(1));
    EXPECT_EQ(finish_calls, 0);

    // Removing the replacement finishes the pump.
    ASSERT_TRUE(display_loop_->Post([replacement] { replacement->Remove(); }).ok());
    display_loop_->RunAll();
    pump->OnEvent(FrameEvent{.display_id = 1});
    worker_loop_->RunAll();
    EXPECT_EQ(finish_calls, 1);
}

TEST_F(LatestFramePumpTest, DestroyingPumpWhileProcessRunsDefersDestructionUntilReturn) {
    auto display = std::make_shared<StubDisplay>(display_loop_.get(), 0, 100, 100);
    display->PublishSolid();

    auto canary = std::make_shared<int>(7);
    const std::weak_ptr<int> watch = canary;
    absl::Notification entered;
    absl::Notification release;
    std::atomic<bool> captures_intact{false};
    auto pump = std::make_shared<LatestFramePump>(
            [display] { return display; }, worker_loop_.get(),
            [&, held = std::move(canary), payload = std::vector<int>(64, 7)](FramePtr /*frame*/) {
                entered.Notify();
                release.WaitForNotification();
                // Reads state owned by the pump after the owner dropped it.
                captures_intact = *held == 7 && payload.back() == 7;
            });
    pump->OnEvent(FrameEvent{.frame_id = 1});

    std::thread driver([&] { worker_loop_->RunAll(); });
    entered.WaitForNotification();
    pump.reset();
    EXPECT_FALSE(watch.expired());  // The running Drain keeps the pump alive.
    release.Notify();
    driver.join();

    EXPECT_TRUE(captures_intact);
    EXPECT_TRUE(watch.expired());  // Destroyed once Drain returned.
}

TEST_F(LatestFramePumpTest, WiredToDisplayFramesListenerAcrossThreads) {
    constexpr int kFrames = 3000;
    constexpr int kFramesPerRotation = 25;
    constexpr ImageRotation kRotations[] = {ImageRotation::kRotation90, ImageRotation::kRotation180,
                                            ImageRotation::kRotation270, ImageRotation::kRotation0};
    auto display = std::make_shared<StubDisplay>(display_loop_.get(), 0, 100, 100);

    struct Processed {
        FrameId id;
        ImageRotation rotation;
    };
    absl::Mutex processed_mutex;
    std::vector<Processed> processed;  // Guarded by processed_mutex.
    auto pump = std::make_shared<LatestFramePump>(
            [display] { return display; }, worker_loop_.get(),
            [&](FramePtr frame) {
                absl::MutexLock lock(&processed_mutex);
                processed.push_back({frame->event().frame_id, frame->rotation()});
            });
    ScopedFrameListener listener = display->Frames()->AddFrameListener(
            [pump](const FrameEvent& event) { pump->OnEvent(event); });

    absl::Notification start;
    std::atomic<bool> publishing{true};
    auto drive = [&](TestEventLoop* loop) {
        start.WaitForNotification();
        while (publishing) {
            loop->RunAll();
            std::this_thread::yield();
        }
    };
    std::thread display_driver(drive, display_loop_.get());
    std::thread worker_driver(drive, worker_loop_.get());
    std::thread publisher([&] {
        start.WaitForNotification();
        for (int i = 0; i < kFrames; ++i) {
            display->PublishSolid();
            std::this_thread::yield();
            if (i % kFramesPerRotation == kFramesPerRotation - 1) {
                display->Frames()->SetRotation(kRotations[(i / kFramesPerRotation) % 4]);
            }
        }
        publishing = false;
    });
    start.Notify();
    publisher.join();
    display_driver.join();
    worker_driver.join();
    // Deliver the tail: events still queued on the display loop, then the drain.
    display_loop_->RunAll();
    worker_loop_->RunAll();

    absl::MutexLock lock(&processed_mutex);
    ASSERT_FALSE(processed.empty());
    EXPECT_EQ(processed.back().id, display->Frames()->CurrentFrameId());
    EXPECT_EQ(processed.back().rotation, display->Frames()->GetRotation());
    for (size_t i = 1; i < processed.size(); ++i) {
        const Processed& prev = processed[i - 1];
        const Processed& cur = processed[i];
        // Newer frames only; the same frame again only with a new rotation.
        EXPECT_TRUE(cur.id > prev.id || (cur.id == prev.id && cur.rotation != prev.rotation))
                << "at " << i << ": " << prev.id << " -> " << cur.id;
    }
}

}  // namespace
}  // namespace goldfish::display
