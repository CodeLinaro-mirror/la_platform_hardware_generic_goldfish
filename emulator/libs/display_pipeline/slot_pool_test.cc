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
#include "goldfish/display/slot_pool.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/synchronization/barrier.h"
#include "gtest/gtest.h"

#include "goldfish/display/frame.h"
#include "goldfish/display/geometry.h"

namespace goldfish::display {
namespace {

constexpr Dimensions kSize{4, 2};

// Slot lifetime counters, safe to update from any thread.
struct SlotCounts {
    std::atomic<int> created{0};
    std::atomic<int> destroyed{0};

    int Live() const { return created.load() - destroyed.load(); }
};

// Counts instances, so tests can see when a slot's memory is freed.
class CountingSlot final : public Slot {
  public:
    explicit CountingSlot(SlotCounts* counts, Dimensions size = kSize)
            : counts_(counts), size_(size) {
        counts_->created.fetch_add(1);
    }
    ~CountingSlot() override { counts_->destroyed.fetch_add(1); }

    Dimensions size() const override { return size_; }

    // Set while one owner holds the slot, to detect a slot handed out twice.
    std::atomic<bool> in_use{false};

  protected:
    absl::Status DoReadInto(const ReadRequest& /*read*/, const ReadTarget& /*dst*/) const override {
        return absl::OkStatus();
    }

  private:
    SlotCounts* const counts_;
    const Dimensions size_;
};

using Pixels = std::array<uint8_t, static_cast<size_t>(kSize.width) * kSize.height * 4>;

ReadTarget TargetOf(Pixels& pixels) {
    return Rgba8888Target{Plane{absl::MakeSpan(pixels)}};
}

class SlotPoolTest : public ::testing::Test {
  protected:
    std::vector<std::unique_ptr<Slot>> MakeSlots(size_t count) {
        std::vector<std::unique_ptr<Slot>> slots;
        slots.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            slots.push_back(std::make_unique<CountingSlot>(&counts_));
        }
        return slots;
    }

    std::shared_ptr<SlotPool> MakePool(size_t capacity) {
        return SlotPool::Create(kSize, MakeSlots(capacity),
                                [this](const SlotPool&) { frees_.fetch_add(1); });
    }

    ReadTarget Target() { return TargetOf(pixels_); }

    Pixels pixels_{};
    SlotCounts counts_;
    std::atomic<int> frees_{0};
};

TEST_F(SlotPoolTest, ReportsItsSizeAndCapacity) {
    std::shared_ptr<SlotPool> pool = MakePool(3);
    EXPECT_EQ(pool->size(), kSize);
    EXPECT_EQ(pool->capacity(), 3U);
}

TEST_F(SlotPoolTest, NeverHandsOutMoreThanCapacitySlots) {
    std::shared_ptr<SlotPool> pool = MakePool(2);
    EXPECT_EQ(pool->capacity(), 2U);
    EXPECT_TRUE(pool->HasCapacity());

    PooledSlot first = pool->TryAcquire();
    PooledSlot second = pool->TryAcquire();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    EXPECT_FALSE(pool->HasCapacity());
    EXPECT_EQ(pool->TryAcquire(), nullptr);
}

TEST_F(SlotPoolTest, ReturnedSlotIsRecycledAndReported) {
    std::shared_ptr<SlotPool> pool = MakePool(1);
    PooledSlot slot = pool->TryAcquire();
    const Slot* const address = slot.get();

    slot.reset();

    EXPECT_EQ(frees_.load(), 1);
    EXPECT_EQ(counts_.Live(), 1);  // Idle, not freed.
    PooledSlot again = pool->TryAcquire();
    EXPECT_EQ(again.get(), address);
}

TEST_F(SlotPoolTest, LeaseReturnsItsSlotToThePool) {
    std::shared_ptr<SlotPool> pool = MakePool(1);
    {
        SlotLease lease(pool->TryAcquire(), kSize);
        EXPECT_FALSE(pool->HasCapacity());
    }
    EXPECT_EQ(frees_.load(), 1);
    EXPECT_TRUE(pool->HasCapacity());
}

TEST_F(SlotPoolTest, MoveAssignOverLeaseReturnsOldSlot) {
    std::shared_ptr<SlotPool> pool = MakePool(2);
    PooledSlot old_slot = pool->TryAcquire();
    const Slot* const old_address = old_slot.get();
    SlotLease lease(std::move(old_slot), kSize);
    SlotLease replacement(pool->TryAcquire(), kSize);
    ASSERT_FALSE(pool->HasCapacity());

    lease = std::move(replacement);

    EXPECT_EQ(frees_.load(), 1);
    EXPECT_EQ(counts_.Live(), 2);
    EXPECT_TRUE(lease.ReadInto({}, Target()).ok());
    PooledSlot recycled = pool->TryAcquire();
    EXPECT_EQ(recycled.get(), old_address);
}

TEST_F(SlotPoolTest, DestroyedPoolFreesIdleSlotsAtOnceAndOutstandingOnesOnReturn) {
    std::shared_ptr<SlotPool> pool = MakePool(2);
    SlotLease leased(pool->TryAcquire(), kSize);
    ASSERT_EQ(counts_.Live(), 2);

    pool.reset();

    EXPECT_EQ(counts_.Live(), 1);  // Only the leased slot is left, and it stays readable.
    EXPECT_TRUE(leased.ReadInto({}, Target()).ok());
    {
        SlotLease dropped = std::move(leased);
    }
    EXPECT_EQ(counts_.Live(), 0);
    EXPECT_EQ(frees_.load(), 0);  // Returned after the pool died.
}

TEST_F(SlotPoolTest, MovedFromLeaseFailsToRead) {
    std::shared_ptr<SlotPool> pool = MakePool(1);
    SlotLease lease(pool->TryAcquire(), kSize);
    SlotLease moved = std::move(lease);

    // NOLINTNEXTLINE(bugprone-use-after-move): the moved-from state is the point.
    EXPECT_TRUE(absl::IsFailedPrecondition(lease.ReadInto({}, Target())));
    EXPECT_TRUE(moved.ReadInto({}, Target()).ok());
}

TEST_F(SlotPoolTest, HomelessLeaseFreesItsSlot) {
    {
        SlotLease lease(std::make_unique<const CountingSlot>(&counts_));
        EXPECT_EQ(lease.dimensions(), kSize);
        EXPECT_EQ(counts_.Live(), 1);
    }
    EXPECT_EQ(counts_.Live(), 0);
}

TEST_F(SlotPoolTest, OnFreeReceivesThisPool) {
    const SlotPool* seen = nullptr;
    std::shared_ptr<SlotPool> pool =
            SlotPool::Create(kSize, MakeSlots(1), [&](const SlotPool& p) { seen = &p; });
    {
        PooledSlot slot = pool->TryAcquire();
    }
    EXPECT_EQ(seen, pool.get());
}

TEST_F(SlotPoolTest, OnFreeRunsWithoutPoolLockHeld) {
    // Re-entering the pool from OnFree deadlocks if `idle_mutex_` is held.
    SlotPool* self = nullptr;
    int calls = 0;
    bool had_capacity = false;
    PooledSlot reacquired;
    std::shared_ptr<SlotPool> pool = SlotPool::Create(kSize, MakeSlots(1), [&](const SlotPool& p) {
        if (++calls > 1) return;
        had_capacity = p.HasCapacity();
        reacquired = self->TryAcquire();
    });
    self = pool.get();

    {
        PooledSlot slot = pool->TryAcquire();
    }

    EXPECT_TRUE(had_capacity);  // The slot is idle before OnFree runs.
    ASSERT_NE(reacquired, nullptr);
    EXPECT_FALSE(pool->HasCapacity());
    PooledSlot last = std::move(reacquired);
    last.reset();
    EXPECT_EQ(calls, 2);
    EXPECT_TRUE(pool->HasCapacity());
}

TEST_F(SlotPoolTest, OnFreeMayDropTheLastPoolReference) {
    std::shared_ptr<SlotPool> pool;
    int calls = 0;
    pool = SlotPool::Create(kSize, MakeSlots(2), [&](const SlotPool&) {
        ++calls;
        pool.reset();
    });
    const std::weak_ptr<SlotPool> watch = pool;
    SlotLease lease(pool->TryAcquire(), kSize);

    {
        SlotLease dropped = std::move(lease);
    }

    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(watch.expired());
    EXPECT_EQ(counts_.Live(), 0);  // Every slot, including the returned one, died with the pool.
}

using SlotPoolDeathTest = SlotPoolTest;

TEST_F(SlotPoolDeathTest, CreateRejectsNullOrMisSizedSlotsInDebugBuilds) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEBUG_DEATH(
            {
                std::vector<std::unique_ptr<Slot>> slots;
                slots.push_back(nullptr);
                SlotPool::Create(kSize, std::move(slots), nullptr);
            },
            "must not be null");
    EXPECT_DEBUG_DEATH(
            {
                std::vector<std::unique_ptr<Slot>> slots;
                slots.push_back(std::make_unique<CountingSlot>(&counts_, Dimensions{1, 1}));
                SlotPool::Create(kSize, std::move(slots), nullptr);
            },
            "pool's size");
}

using SlotPoolConcurrencyTest = SlotPoolTest;

TEST_F(SlotPoolConcurrencyTest, AcquireReleaseAcrossThreadsNeverExceedsCapacity) {
    constexpr int kCapacity = 3;
    constexpr int kThreads = 8;
    constexpr int kIterations = 2000;
    std::shared_ptr<SlotPool> pool = MakePool(kCapacity);
    absl::Barrier start(kThreads);
    std::atomic<int> out{0};
    std::atomic<int> max_out{0};
    std::atomic<int> acquired{0};
    std::atomic<int> double_handouts{0};

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            Pixels pixels{};
            start.Block();
            for (int i = 0; i < kIterations; ++i) {
                PooledSlot slot = pool->TryAcquire();
                if (slot == nullptr) {
                    std::this_thread::yield();
                    continue;
                }
                acquired.fetch_add(1);
                auto& counting = static_cast<CountingSlot&>(*slot);
                if (counting.in_use.exchange(true)) double_handouts.fetch_add(1);
                const int now = out.fetch_add(1) + 1;
                int seen = max_out.load();
                while (now > seen && !max_out.compare_exchange_weak(seen, now)) {
                }
                EXPECT_TRUE(slot->ReadInto({}, TargetOf(pixels)).ok());
                out.fetch_sub(1);
                counting.in_use.store(false);
                // Half the slots come back through a lease, as a published frame would.
                if ((t + i) % 2 == 0) {
                    SlotLease lease(std::move(slot), kSize);
                }
            }
        });
    }
    for (std::thread& thread : threads) thread.join();

    EXPECT_EQ(double_handouts.load(), 0);
    EXPECT_LE(max_out.load(), kCapacity);
    EXPECT_GT(acquired.load(), 0);
    EXPECT_EQ(frees_.load(), acquired.load());
    EXPECT_EQ(counts_.Live(), kCapacity);
    std::vector<PooledSlot> all;
    for (int i = 0; i < kCapacity; ++i) {
        all.push_back(pool->TryAcquire());
        ASSERT_NE(all.back(), nullptr);
    }
    EXPECT_EQ(pool->TryAcquire(), nullptr);
}

TEST_F(SlotPoolConcurrencyTest, LeasesDroppedOnWorkersWhilePoolIsReset) {
    constexpr int kRounds = 200;
    constexpr int kLeased = 4;
    for (int round = 0; round < kRounds; ++round) {
        std::atomic<int> round_frees{0};
        // One slot stays idle, so the reset frees memory too.
        std::shared_ptr<SlotPool> pool =
                SlotPool::Create(kSize, MakeSlots(kLeased + 1), [&](const SlotPool& p) {
                    round_frees.fetch_add(1);
                    EXPECT_TRUE(p.HasCapacity());
                });
        absl::Barrier start(kLeased + 1);
        std::vector<std::thread> workers;
        workers.reserve(kLeased);
        for (int i = 0; i < kLeased; ++i) {
            workers.emplace_back([&start, lease = SlotLease(pool->TryAcquire(), kSize)]() mutable {
                Pixels pixels{};
                start.Block();
                EXPECT_TRUE(lease.ReadInto({}, TargetOf(pixels)).ok());
                SlotLease dropped = std::move(lease);
            });
        }
        start.Block();
        pool.reset();
        for (std::thread& worker : workers) worker.join();

        ASSERT_EQ(counts_.Live(), 0) << "round " << round;
        ASSERT_LE(round_frees.load(), kLeased) << "round " << round;
    }
    EXPECT_EQ(counts_.created.load(), kRounds * (kLeased + 1));
    EXPECT_EQ(counts_.destroyed.load(), counts_.created.load());
}

}  // namespace
}  // namespace goldfish::display
