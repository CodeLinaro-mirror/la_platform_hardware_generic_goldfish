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
#include "goldfish/display/legacy_surface_source.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/barrier.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "goldfish/async/testing/test_event_loop.h"
#include "goldfish/display/frame.h"
#include "goldfish/display/frame_producer.h"
#include "goldfish/display/frame_source.h"
#include "goldfish/display/geometry.h"
#include "goldfish/display/slot_storage.h"
#include "goldfish/display/test/fake_slot_storage.h"

namespace goldfish::display {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::goldfish::async::testing::TestEventLoop;
using ::goldfish::display::testing::FakeSlotStorage;
using ::testing::ElementsAre;
using Start = IFrameSource::Start;

constexpr Dimensions kSize{4, 2};
constexpr Dimensions kOtherSize{8, 4};

// Reads the fake storage's content tag of a lease: the first byte of the
// surface it copied.
absl::StatusOr<uint8_t> ReadTag(const SlotLease& lease) {
    std::array<uint8_t, 4> pixel{};
    absl::Status status =
            lease.ReadInto({.output = {1, 1}}, Rgba8888Target{Plane{absl::MakeSpan(pixel)}});
    if (!status.ok()) return status;
    return pixel[0];
}

// Counts the re-capture requests of the source under test.
class CountingProducer final : public IFrameProducer {
  public:
    void SetDemand(bool /*warm*/) override {}
    void RequestFrame() override { ++requests; }

    int requests = 0;
};

// A FakeSlotStorage whose n-th next allocation fails, so a pool can fail
// partway through being built.
class FailingNthAllocationStorage final : public ISlotStorage {
  public:
    explicit FailingNthAllocationStorage(std::unique_ptr<FakeSlotStorage> storage)
            : storage_(std::move(storage)) {}

    void FailAllocation(int nth) { countdown_ = nth; }

    absl::StatusOr<std::unique_ptr<Slot>> Allocate(Dimensions size) override {
        if (countdown_ > 0 && --countdown_ == 0) {
            return absl::ResourceExhaustedError("No memory");
        }
        return storage_->Allocate(size);
    }
    void Capture(Slot& dst, const SurfaceSource& src,
                 absl::AnyInvocable<void(CaptureResult) &&> done) override {
        storage_->Capture(dst, src, std::move(done));
    }

  private:
    const std::unique_ptr<FakeSlotStorage> storage_;
    int countdown_ = 0;
};

class LegacySurfaceSourceTest : public ::testing::Test {
  protected:
    static constexpr size_t kSlots = 2;

    // Captures the current surface; the fake storage completes inline.
    absl::StatusOr<SlotLease> CaptureLease() {
        std::optional<absl::StatusOr<SlotLease>> frame;
        const Start start = source_->Capture(CaptureCompletion(
                [&frame](absl::StatusOr<SlotLease> result) { frame = std::move(result); }));
        EXPECT_EQ(start, Start::kStarted);
        if (!frame.has_value()) return absl::InternalError("Not completed inline");
        return *std::move(frame);
    }

    // Captures and expects that none starts.
    Start CaptureNothing() {
        bool completed = false;
        const Start start =
                source_->Capture(CaptureCompletion([&completed](absl::StatusOr<SlotLease> result) {
                    EXPECT_THAT(result, StatusIs(absl::StatusCode::kCancelled));
                    completed = true;
                }));
        EXPECT_TRUE(completed) << "A capture that doesn't start drops its completion";
        return start;
    }

    // Leases every slot of the current pool; the next capture finds none.
    void Starve() {
        for (size_t i = 0; i < kSlots; ++i) {
            absl::StatusOr<SlotLease> lease = CaptureLease();
            ASSERT_THAT(lease, IsOk());
            leases_.push_back(*std::move(lease));
        }
        ASSERT_EQ(CaptureNothing(), Start::kNoSlot);
        ASSERT_EQ(producer_->requests, 0);
    }

    Start CaptureSurface(LegacySurfaceSource& source) {
        if (!has_content_) return Start::kNoContent;
        return source.CaptureFrom(SurfaceSource{
            .data = &pixel_,
            .dimensions = size_,
            .stride = size_t{size_.width} * 4,
            .pixman_format = 0,
        });
    }

    LegacySurfaceSource::Hooks MakeHooks() {
        return LegacySurfaceSource::Hooks{
            .refresh = [this] { ++refreshes_; },
            .demand_changed = [this](bool warm) { demand_changes_.push_back(warm); },
            .capture = [this](LegacySurfaceSource& source) { return CaptureSurface(source); },
        };
    }

    std::shared_ptr<LegacySurfaceSource> MakeSource(size_t slot_count = kSlots) {
        auto storage = std::make_unique<FakeSlotStorage>();
        storage->set_complete_inline(true);
        storage_ = storage.get();
        return LegacySurfaceSource::Create(producer_, MakeHooks(), std::move(storage), slot_count);
    }

    uint8_t pixel_ = 7;
    Dimensions size_ = kSize;
    bool has_content_ = true;
    int refreshes_ = 0;
    std::vector<bool> demand_changes_;
    std::shared_ptr<CountingProducer> producer_ = std::make_shared<CountingProducer>();
    FakeSlotStorage* storage_ = nullptr;  // Owned by source_.
    std::shared_ptr<LegacySurfaceSource> source_ = MakeSource();
    std::vector<SlotLease> leases_;
};

TEST_F(LegacySurfaceSourceTest, CapturesTheSurfaceIntoAPooledSlot) {
    absl::StatusOr<SlotLease> lease = CaptureLease();
    ASSERT_THAT(lease, IsOk());
    EXPECT_EQ(lease->dimensions(), kSize);
    EXPECT_EQ(storage_->stats()->Captures(), 1);
    EXPECT_EQ(storage_->stats()->Allocations(), static_cast<int>(kSlots));

    lease = absl::CancelledError("Released");
    ASSERT_THAT(CaptureLease(), IsOk());
    EXPECT_EQ(storage_->stats()->Allocations(), static_cast<int>(kSlots));  // Pool is reused.
}

TEST_F(LegacySurfaceSourceTest, NoContentFromTheHookStartsNothing) {
    has_content_ = false;
    EXPECT_EQ(CaptureNothing(), Start::kNoContent);
    EXPECT_EQ(storage_->stats()->Allocations(), 0);
}

TEST_F(LegacySurfaceSourceTest, RefreshAndDemandChangesReachTheHooks) {
    source_->Refresh();
    source_->OnDemandChanged(true);
    source_->OnDemandChanged(false);
    EXPECT_EQ(refreshes_, 1);
    EXPECT_THAT(demand_changes_, ElementsAre(true, false));
}

TEST_F(LegacySurfaceSourceTest, AReleaseAfterExhaustionRequestsOneCapture) {
    Starve();
    leases_.pop_back();
    EXPECT_EQ(producer_->requests, 1);
    leases_.pop_back();
    EXPECT_EQ(producer_->requests, 1);  // One request covers the skipped capture.
}

TEST_F(LegacySurfaceSourceTest, AReleaseWithoutExhaustionRequestsNothing) {
    absl::StatusOr<SlotLease> lease = CaptureLease();
    ASSERT_THAT(lease, IsOk());
    lease = absl::CancelledError("Released");
    EXPECT_EQ(producer_->requests, 0);
}

TEST_F(LegacySurfaceSourceTest, ANewSizeCapturesIntoANewPoolWhoseOldSlotsRequestNothing) {
    Starve();
    size_ = kOtherSize;
    absl::StatusOr<SlotLease> lease = CaptureLease();
    ASSERT_THAT(lease, IsOk());
    EXPECT_EQ(lease->dimensions(), kOtherSize);
    EXPECT_EQ(storage_->stats()->Allocations(), static_cast<int>(kSlots * 2));

    leases_.clear();
    EXPECT_EQ(producer_->requests, 0);
}

// Only a slot returned to the currently active pool satisfies its starvation
// debt; slots returned to a superseded pool must be ignored.
TEST_F(LegacySurfaceSourceTest, ASlotOfAnOlderPoolDoesNotPayTheCurrentPoolsDebt) {
    Starve();
    std::vector<SlotLease> old_leases = std::move(leases_);
    leases_.clear();
    size_ = kOtherSize;
    Starve();

    old_leases.clear();
    EXPECT_EQ(producer_->requests, 0);
    leases_.pop_back();
    EXPECT_EQ(producer_->requests, 1);
}

TEST_F(LegacySurfaceSourceTest, ResetDropsThePoolAndItsDebt) {
    Starve();
    source_->Reset();
    leases_.clear();  // Slots from before the reset belong to a retired pool.
    EXPECT_EQ(producer_->requests, 0);
    ASSERT_THAT(CaptureLease(), IsOk());
    EXPECT_EQ(storage_->stats()->Allocations(), static_cast<int>(kSlots * 2));
}

TEST_F(LegacySurfaceSourceTest, ResizeRequestsTheCaptureOwedToTheOldPool) {
    Starve();
    source_->Resize(kOtherSize);
    EXPECT_EQ(producer_->requests, 1);
    leases_.clear();
    EXPECT_EQ(producer_->requests, 1);
}

TEST_F(LegacySurfaceSourceTest, ResizeToTheSameSizeKeepsThePoolAndTheDebt) {
    Starve();
    source_->Resize(kSize);
    EXPECT_EQ(producer_->requests, 0);
    leases_.pop_back();
    EXPECT_EQ(producer_->requests, 1);
}

TEST_F(LegacySurfaceSourceTest, ColdForgetsTheOwedCapture) {
    Starve();
    source_->OnDemandChanged(false);
    leases_.clear();
    EXPECT_EQ(producer_->requests, 0);
}

// A failed allocation owes nothing a release could pay, so only the next
// capture retries it.
TEST_F(LegacySurfaceSourceTest, AllocationFailureIsRetriedByTheNextCaptureNotByARelease) {
    absl::StatusOr<SlotLease> lease = CaptureLease();
    ASSERT_THAT(lease, IsOk());
    size_ = kOtherSize;
    storage_->FailNextAllocation(absl::ResourceExhaustedError("No memory"));
    EXPECT_EQ(CaptureNothing(), Start::kNoSlot);

    lease = absl::CancelledError("Released");
    EXPECT_EQ(producer_->requests, 0);
    lease = CaptureLease();
    ASSERT_THAT(lease, IsOk());
    EXPECT_EQ(storage_->stats()->Allocations(), static_cast<int>(kSlots * 2));
}

TEST_F(LegacySurfaceSourceTest, AFailedCaptureReportsItsErrorAndReturnsItsSlot) {
    storage_->set_complete_inline(false);
    std::optional<absl::StatusOr<SlotLease>> frame;
    ASSERT_EQ(source_->Capture(CaptureCompletion(
                      [&frame](absl::StatusOr<SlotLease> result) { frame = std::move(result); })),
              Start::kStarted);
    ASSERT_TRUE(storage_->CompleteNext(absl::DataLossError("Bad surface")));
    ASSERT_TRUE(frame.has_value());
    EXPECT_THAT(*frame, StatusIs(absl::StatusCode::kDataLoss));

    storage_->set_complete_inline(true);
    ASSERT_THAT(CaptureLease(), IsOk());
    EXPECT_EQ(storage_->stats()->Allocations(), static_cast<int>(kSlots));
}

// After a reset, only a slot of the new pool satisfies the new pool's
// starvation debt.
TEST_F(LegacySurfaceSourceTest, ASlotFromBeforeAResetDoesNotPayTheNewPoolsDebt) {
    Starve();
    std::vector<SlotLease> old_leases = std::move(leases_);
    leases_.clear();
    source_->Reset();
    Starve();

    old_leases.clear();
    EXPECT_EQ(producer_->requests, 0);
    leases_.pop_back();
    EXPECT_EQ(producer_->requests, 1);
}

// Neither a reset nor a new size touches a frame already out: it stays
// readable (the sink's epoch decides what consumers may read), and its
// slot is freed, not recycled, when it is released.
TEST_F(LegacySurfaceSourceTest, ALeaseOfADroppedPoolStaysReadableUntilReleased) {
    for (const bool reset : {true, false}) {
        SCOPED_TRACE(reset ? "Reset" : "Resize");
        size_ = kSize;
        source_ = MakeSource();
        pixel_ = 3;
        absl::StatusOr<SlotLease> lease = CaptureLease();
        ASSERT_THAT(lease, IsOk());
        if (reset) {
            source_->Reset();
        } else {
            source_->Resize(kOtherSize);
            size_ = kOtherSize;
        }
        pixel_ = 4;
        ASSERT_THAT(CaptureLease(), IsOk());  // Released at once, into the new pool.

        EXPECT_THAT(ReadTag(*lease), ::absl_testing::IsOkAndHolds(3));
        EXPECT_EQ(lease->dimensions(), kSize);
        EXPECT_EQ(storage_->stats()->LiveBuffers(), static_cast<int>(kSlots) + 1);
        lease = absl::CancelledError("Released");
        EXPECT_EQ(storage_->stats()->LiveBuffers(),
                  static_cast<int>(kSlots));  // Freed, not given to the new pool.
    }
}

TEST_F(LegacySurfaceSourceTest, ALeaseOutlivesItsSource) {
    Starve();
    const std::shared_ptr<testing::FakeStorageStats> stats = storage_->stats();
    source_.reset();
    storage_ = nullptr;
    EXPECT_THAT(ReadTag(leases_.back()), ::absl_testing::IsOkAndHolds(pixel_));
    leases_.clear();
    EXPECT_EQ(producer_->requests, 0);  // The starved source is gone.
    EXPECT_EQ(stats->LiveBuffers(), 0);
}

// A hook that reports a capture it never started must not strand the
// producer: the completion is cancelled, exactly once, before Capture returns.
TEST_F(LegacySurfaceSourceTest, HookClaimingStartWithoutCaptureCancels) {
    auto storage = std::make_unique<FakeSlotStorage>();
    const std::shared_ptr<testing::FakeStorageStats> stats = storage->stats();
    source_ = LegacySurfaceSource::Create(
            producer_,
            LegacySurfaceSource::Hooks{
                .capture = [](LegacySurfaceSource& /*source*/) { return Start::kStarted; }},
            std::move(storage), kSlots);
    storage_ = nullptr;
    int completions = 0;
    absl::Status status;
    EXPECT_EQ(source_->Capture(CaptureCompletion([&](absl::StatusOr<SlotLease> result) {
        ++completions;
        status = result.status();
    })),
              Start::kStarted);
    EXPECT_EQ(completions, 1);
    EXPECT_THAT(status, StatusIs(absl::StatusCode::kCancelled));
    EXPECT_EQ(stats->Allocations(), 0);
}

TEST_F(LegacySurfaceSourceTest, ZeroSlotCountClampsToOne) {
    source_ = MakeSource(/*slot_count=*/0);
    absl::StatusOr<SlotLease> lease = CaptureLease();
    ASSERT_THAT(lease, IsOk());
    EXPECT_EQ(storage_->stats()->Allocations(), 1);
    EXPECT_EQ(CaptureNothing(), Start::kNoSlot);
    lease = absl::CancelledError("Released");
    EXPECT_EQ(producer_->requests, 1);
}

// The debt is gone with the pool: nothing from the new pool requests a frame
// until the new pool starves in turn.
TEST_F(LegacySurfaceSourceTest, ResetClearsStarvedFlag) {
    Starve();
    source_->Reset();
    ASSERT_THAT(CaptureLease(), IsOk());  // Released at once, into the new pool.
    leases_.clear();
    EXPECT_EQ(producer_->requests, 0);

    Starve();
    leases_.pop_back();
    EXPECT_EQ(producer_->requests, 1);
}

TEST_F(LegacySurfaceSourceTest, ResizeOrResetBeforeFirstCapture) {
    source_->Resize(kOtherSize);
    source_->Reset();
    source_->Resize(kSize);
    EXPECT_EQ(producer_->requests, 0);
    EXPECT_EQ(storage_->stats()->Allocations(), 0);

    absl::StatusOr<SlotLease> lease = CaptureLease();
    ASSERT_THAT(lease, IsOk());
    EXPECT_EQ(lease->dimensions(), kSize);
    EXPECT_EQ(storage_->stats()->Allocations(), static_cast<int>(kSlots));
}

// A pool whose second slot fails to allocate frees its first one, owes
// nothing a release could pay, and is rebuilt whole by the next capture.
TEST_F(LegacySurfaceSourceTest, PartialAllocationFailureLeaksNothing) {
    constexpr int kPoolSlots = 3;
    auto fake = std::make_unique<FakeSlotStorage>();
    fake->set_complete_inline(true);
    const std::shared_ptr<testing::FakeStorageStats> stats = fake->stats();
    auto failing = std::make_unique<FailingNthAllocationStorage>(std::move(fake));
    FailingNthAllocationStorage* storage = failing.get();
    source_ = LegacySurfaceSource::Create(producer_, MakeHooks(), std::move(failing), kPoolSlots);
    storage_ = nullptr;

    absl::StatusOr<SlotLease> lease = CaptureLease();
    ASSERT_THAT(lease, IsOk());
    size_ = kOtherSize;
    storage->FailAllocation(2);
    EXPECT_EQ(CaptureNothing(), Start::kNoSlot);
    EXPECT_EQ(stats->Allocations(), kPoolSlots + 1);
    // Only the leased slot is left: the half-built pool's slot and the idle
    // slots of the pool it was replacing are freed.
    EXPECT_EQ(stats->LiveBuffers(), 1);
    EXPECT_THAT(ReadTag(*lease), ::absl_testing::IsOkAndHolds(pixel_));
    lease = absl::CancelledError("Released");
    EXPECT_EQ(stats->LiveBuffers(), 0);
    EXPECT_EQ(producer_->requests, 0);

    lease = CaptureLease();
    ASSERT_THAT(lease, IsOk());
    EXPECT_EQ(lease->dimensions(), kOtherSize);
    EXPECT_EQ(stats->LiveBuffers(), kPoolSlots);
    lease = absl::CancelledError("Released");
    source_.reset();
    EXPECT_EQ(stats->LiveBuffers(), 0);
}

// Leases are read and released on consumer threads while the producer's
// thread keeps capturing, switching sizes and going cold, and another thread
// resets and resizes until the captures are done. A slot overwritten while
// leased, or handed over before its copy completed, reads a tag other than
// the one it had when it was captured. Afterwards the source must still
// recover from starvation.
TEST(LegacySurfaceSourceConcurrencyTest, CaptureReadReleaseAndResetOnManyThreads) {
    class AtomicCountingProducer final : public IFrameProducer {
      public:
        void SetDemand(bool /*warm*/) override {}
        void RequestFrame() override { requests.fetch_add(1); }
        std::atomic<int> requests{0};
    };
    struct Captured {
        SlotLease lease;
        uint8_t tag;
    };
    constexpr int kCaptures = 2000;
    constexpr int kReaders = 3;
    constexpr size_t kPoolSlots = 4;
    absl::Mutex queue_mutex;
    std::deque<Captured> queue;  // Guarded by queue_mutex.

    auto producer = std::make_shared<AtomicCountingProducer>();
    auto storage = std::make_unique<FakeSlotStorage>();
    storage->set_complete_inline(true);
    const std::shared_ptr<testing::FakeStorageStats> stats = storage->stats();
    // Only touched on this thread, which drives the source like a producer.
    uint8_t pixel = 0;
    Dimensions size = kSize;
    auto source =
            LegacySurfaceSource::Create(producer,
                                        LegacySurfaceSource::Hooks{
                                            .capture =
                                                    [&pixel, &size](LegacySurfaceSource& source) {
                                                        return source.CaptureFrom(SurfaceSource{
                                                            .data = &pixel,
                                                            .dimensions = size,
                                                            .stride = size_t{size.width} * 4});
                                                    },
                                        },
                                        std::move(storage), kPoolSlots);

    absl::Barrier start(kReaders + 2);
    std::atomic<bool> captures_done{false};
    std::vector<std::thread> threads;
    for (int i = 0; i < kReaders; ++i) {
        threads.emplace_back([&] {
            start.Block();
            while (!captures_done.load()) {
                std::optional<Captured> captured;
                {
                    absl::MutexLock lock(&queue_mutex);
                    if (!queue.empty()) {
                        captured = std::move(queue.front());
                        queue.pop_front();
                    }
                }
                if (!captured.has_value()) {
                    std::this_thread::yield();
                    continue;
                }
                EXPECT_THAT(ReadTag(captured->lease), ::absl_testing::IsOkAndHolds(captured->tag));
            }
        });
    }
    threads.emplace_back([&] {
        start.Block();
        for (int i = 0; !captures_done.load(); ++i) {
            switch (i % 3) {
            case 0:
                source->Reset();
                break;
            case 1:
                source->Resize(kOtherSize);
                break;
            default:
                source->Resize(kSize);
                break;
            }
            std::this_thread::yield();
        }
    });
    start.Block();
    for (int i = 0; i < kCaptures; ++i) {
        if (i % 97 == 0) size = size == kSize ? kOtherSize : kSize;
        if (i % 101 == 50) {
            source->OnDemandChanged(false);
            source->OnDemandChanged(true);
        }
        pixel = static_cast<uint8_t>(i);
        (void)source->Capture(CaptureCompletion(
                [&, tag = pixel, captured_size = size](absl::StatusOr<SlotLease> frame) {
                    if (!frame.ok()) return;
                    EXPECT_EQ(frame->dimensions(), captured_size);
                    EXPECT_THAT(ReadTag(*frame), ::absl_testing::IsOkAndHolds(tag));
                    absl::MutexLock lock(&queue_mutex);
                    queue.push_back(Captured{*std::move(frame), tag});
                }));
    }
    captures_done = true;
    for (std::thread& thread : threads) thread.join();

    {
        absl::MutexLock lock(&queue_mutex);
        queue.clear();
    }
    // Liveness: whatever state the race left behind, a starved source asks
    // for a frame once its leases come back.
    std::vector<SlotLease> held;
    Start started = Start::kStarted;
    for (size_t i = 0; i <= kPoolSlots && started == Start::kStarted; ++i) {
        started = source->Capture(CaptureCompletion([&held](absl::StatusOr<SlotLease> frame) {
            if (frame.ok()) held.push_back(*std::move(frame));
        }));
    }
    ASSERT_EQ(started, Start::kNoSlot);
    EXPECT_EQ(held.size(), kPoolSlots);
    const int requests = producer->requests.load();
    held.clear();
    EXPECT_EQ(producer->requests.load(), requests + 1);

    source.reset();
    EXPECT_EQ(stats->LiveBuffers(), 0);
}

// Runs `body` on another thread and aborts if it does not finish in time, so
// a self-deadlock fails the test instead of hanging it.
void RunWithDeadlockGuard(const std::function<void()>& body) {
    absl::Notification done;
    std::thread runner([&] {
        body();
        done.Notify();
    });
    if (!done.WaitForNotificationWithTimeout(absl::Seconds(10))) LOG(FATAL) << "Deadlocked";
    runner.join();
}

// Keeps the latest frame and drops the one it replaces inside PublishFrame,
// like DisplayFrames.
class LatestFrameSink final : public IFrameSink {
  public:
    void PublishFrame(SlotLease lease, absl::Time /*capture_time*/) override {
        std::optional<SlotLease> replaced;  // Dropped after the lock is released.
        absl::MutexLock sink_lock(&sink_mutex_);
        ++frames_;
        replaced = std::exchange(latest_, std::move(lease));
    }
    void OnProducerReset() override {}
    void PublishNoContent() override {}

    int Frames() {
        absl::MutexLock sink_lock(&sink_mutex_);
        return frames_;
    }

  private:
    absl::Mutex sink_mutex_;
    int frames_ ABSL_GUARDED_BY(sink_mutex_) = 0;
    std::optional<SlotLease> latest_ ABSL_GUARDED_BY(sink_mutex_);
};

// Captures from the legacy source, or from another source while the legacy
// source is starved.
class SwitchingSource final : public IFrameSource {
  public:
    explicit SwitchingSource(std::shared_ptr<LegacySurfaceSource> legacy)
            : legacy_(std::move(legacy)) {}

    void Refresh() override { legacy_->Refresh(); }
    Start Capture(CaptureCompletion done) override {
        if (!use_other) return legacy_->Capture(std::move(done));
        std::move(done)(SlotLease(std::unique_ptr<const Slot>(*other_.Allocate(kSize))));
        return Start::kStarted;
    }

    bool use_other = false;

  private:
    const std::shared_ptr<LegacySurfaceSource> legacy_;
    FakeSlotStorage other_;
};

// The sink drops a starved pool's slot while the producer holds its publish
// mutex, so the re-capture request must only post to the executor rather than
// re-entering the producer inline.
TEST(LegacySurfaceSourceProducerTest, ASlotReleasedInsidePublishFramePostsOneCapture) {
    std::unique_ptr<TestEventLoop> loop = TestEventLoop::Create("executor");
    auto sink = std::make_shared<LatestFrameSink>();
    auto producer = std::make_shared<FrameProducer>(loop.get(), sink);
    auto storage = std::make_unique<FakeSlotStorage>();
    storage->set_complete_inline(true);
    const uint8_t pixel = 1;
    int refreshes = 0;
    auto legacy =
            LegacySurfaceSource::Create(producer,
                                        LegacySurfaceSource::Hooks{
                                            .refresh = [&refreshes] { ++refreshes; },
                                            .capture =
                                                    [&pixel](LegacySurfaceSource& source) {
                                                        return source.CaptureFrom(SurfaceSource{
                                                            .data = &pixel,
                                                            .dimensions = kSize,
                                                            .stride = size_t{kSize.width} * 4});
                                                    },
                                        },
                                        std::move(storage), /*slot_count=*/1);
    auto source = std::make_shared<SwitchingSource>(legacy);
    producer->SetSource(source);

    producer->SetDemand(true);
    loop->RunAll();
    ASSERT_EQ(sink->Frames(), 1);  // The sink holds the pool's only slot.
    producer->Invalidate();        // kNoSlot: the legacy source is starved.
    ASSERT_EQ(loop->TaskCount(), 0);

    source->use_other = true;
    RunWithDeadlockGuard([&producer] { producer->Invalidate(); });
    EXPECT_EQ(sink->Frames(), 2);
    EXPECT_EQ(loop->TaskCount(), 1);  // Posted, not captured inline.

    source->use_other = false;
    loop->RunAll();
    EXPECT_EQ(refreshes, 2);
    EXPECT_EQ(sink->Frames(), 3);
    producer->Shutdown();
}

}  // namespace
}  // namespace goldfish::display
