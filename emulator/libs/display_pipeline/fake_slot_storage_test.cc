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
#include "goldfish/display/test/fake_slot_storage.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"

#include "goldfish/display/frame.h"
#include "goldfish/display/geometry.h"
#include "goldfish/display/slot_pool.h"
#include "goldfish/display/slot_storage.h"

namespace goldfish::display::testing {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::Each;

constexpr Dimensions kSize{4, 2};

// A surface whose content tag (its first byte) is `tag`. The fake reads nothing else.
struct TaggedSurface {
    explicit TaggedSurface(uint8_t tag) : pixels{tag, 0, 0, 0} {}
    SurfaceSource Source() const {
        return SurfaceSource{.data = pixels.data(), .dimensions = kSize, .stride = 4};
    }
    std::vector<uint8_t> pixels;
};

// Reads `slot` as RGBA; every byte holds the slot's content tag.
std::vector<uint8_t> ReadAll(const Slot& slot) {
    std::vector<uint8_t> rgba(static_cast<size_t>(kSize.width) * kSize.height * 4, 0xEE);
    EXPECT_THAT(slot.ReadInto({}, Rgba8888Target{Plane{absl::MakeSpan(rgba)}}), IsOk());
    return rgba;
}

std::unique_ptr<Slot> AllocateOrDie(FakeSlotStorage& storage) {
    absl::StatusOr<std::unique_ptr<Slot>> slot = storage.Allocate(kSize);
    EXPECT_THAT(slot, IsOk());
    return slot.ok() ? *std::move(slot) : nullptr;
}

// Records the result a capture completed with.
struct Completion {
    auto Callback() {
        return [this](CaptureResult result) { this->result = std::move(result); };
    }
    std::optional<CaptureResult> result;
};

TEST(FakeSlotStorageTest, CompleteNextStoresTheContentTagAndReportsTheSlotSize) {
    FakeSlotStorage storage;
    std::unique_ptr<Slot> slot = AllocateOrDie(storage);
    ASSERT_NE(slot, nullptr);
    Completion completion;

    storage.Capture(*slot, TaggedSurface(0x42).Source(), completion.Callback());
    EXPECT_EQ(storage.PendingCaptures(), 1);
    EXPECT_FALSE(completion.result.has_value());

    ASSERT_TRUE(storage.CompleteNext());
    ASSERT_TRUE(completion.result.has_value());
    EXPECT_THAT(completion.result->status, IsOk());
    EXPECT_EQ(completion.result->dimensions, kSize);
    EXPECT_THAT(ReadAll(*slot), Each(0x42));
    EXPECT_EQ(storage.stats()->Captures(), 1);
    EXPECT_EQ(storage.stats()->Reads(), 1);

    EXPECT_FALSE(storage.CompleteNext());
}

TEST(FakeSlotStorageTest, CompleteNextWithAnErrorLeavesTheSlotContent) {
    FakeSlotStorage storage;
    std::unique_ptr<Slot> slot = AllocateOrDie(storage);
    ASSERT_NE(slot, nullptr);
    Completion completion;

    storage.Capture(*slot, TaggedSurface(0x42).Source(), completion.Callback());
    ASSERT_TRUE(storage.CompleteNext(absl::DataLossError("bad surface")));

    ASSERT_TRUE(completion.result.has_value());
    EXPECT_THAT(completion.result->status, StatusIs(absl::StatusCode::kDataLoss));
    EXPECT_THAT(ReadAll(*slot), Each(0x00));
}

TEST(FakeSlotStorageTest, CompletesInSubmissionOrder) {
    FakeSlotStorage storage;
    std::unique_ptr<Slot> first = AllocateOrDie(storage);
    std::unique_ptr<Slot> second = AllocateOrDie(storage);
    ASSERT_TRUE(first != nullptr && second != nullptr);
    Completion first_done;
    Completion second_done;

    storage.Capture(*first, TaggedSurface(1).Source(), first_done.Callback());
    storage.Capture(*second, TaggedSurface(2).Source(), second_done.Callback());
    ASSERT_TRUE(storage.CompleteNext());

    EXPECT_TRUE(first_done.result.has_value());
    EXPECT_FALSE(second_done.result.has_value());
    EXPECT_THAT(ReadAll(*first), Each(1));
    EXPECT_EQ(storage.PendingCaptures(), 1);
}

TEST(FakeSlotStorageTest, DropNextDestroysDoneUncalledAndReturnsTheSlot) {
    FakeSlotStorage storage;
    std::vector<std::unique_ptr<Slot>> slots;
    slots.push_back(AllocateOrDie(storage));
    int frees = 0;
    std::shared_ptr<SlotPool> pool =
            SlotPool::Create(kSize, std::move(slots), [&](const SlotPool&) { ++frees; });
    PooledSlot pooled = pool->TryAcquire();
    ASSERT_NE(pooled, nullptr);
    Slot& dst = *pooled;
    bool called = false;

    // Like a real source, `done` owns the destination slot.
    storage.Capture(dst, TaggedSurface(7).Source(),
                    [&called, owned = std::move(pooled)](CaptureResult) { called = true; });
    EXPECT_FALSE(pool->HasCapacity());

    ASSERT_TRUE(storage.DropNext());
    EXPECT_FALSE(called);
    EXPECT_EQ(frees, 1);
    EXPECT_TRUE(pool->HasCapacity());
    EXPECT_EQ(storage.PendingCaptures(), 0);
    EXPECT_FALSE(storage.DropNext());
}

TEST(FakeSlotStorageTest, DropCompletionsDestroysDoneUncalled) {
    FakeSlotStorage storage;
    std::unique_ptr<Slot> slot = AllocateOrDie(storage);
    ASSERT_NE(slot, nullptr);
    auto alive = std::make_shared<int>(0);
    const std::weak_ptr<int> watch = alive;
    bool called = false;

    storage.set_drop_completions(true);
    storage.Capture(*slot, TaggedSurface(7).Source(),
                    [&called, held = std::move(alive)](CaptureResult) { called = true; });

    EXPECT_FALSE(called);
    EXPECT_TRUE(watch.expired());
    EXPECT_EQ(storage.PendingCaptures(), 0);
    EXPECT_EQ(storage.stats()->Captures(), 1);
}

TEST(FakeSlotStorageTest, FailNextAllocationFailsExactlyOnce) {
    FakeSlotStorage storage;
    storage.FailNextAllocation(absl::ResourceExhaustedError("no memory"));

    EXPECT_THAT(storage.Allocate(kSize), StatusIs(absl::StatusCode::kResourceExhausted));
    absl::StatusOr<std::unique_ptr<Slot>> slot = storage.Allocate(kSize);
    ASSERT_THAT(slot, IsOk());
    EXPECT_EQ((*slot)->size(), kSize);
    EXPECT_EQ(storage.stats()->Allocations(), 1);
    EXPECT_EQ(storage.stats()->LiveBuffers(), 1);
}

TEST(FakeSlotStorageTest, LiveBuffersTracksSlotsBeyondTheStoragesLifetime) {
    auto storage = std::make_unique<FakeSlotStorage>();
    const std::shared_ptr<FakeStorageStats> stats = storage->stats();
    std::unique_ptr<Slot> first = AllocateOrDie(*storage);
    std::unique_ptr<Slot> second = AllocateOrDie(*storage);
    EXPECT_EQ(stats->LiveBuffers(), 2);

    first.reset();
    EXPECT_EQ(stats->LiveBuffers(), 1);

    storage.reset();
    EXPECT_EQ(stats->LiveBuffers(), 1);
    second.reset();
    EXPECT_EQ(stats->LiveBuffers(), 0);
    EXPECT_EQ(stats->Allocations(), 2);
}

TEST(FakeSlotStorageTest, InlineCompletionCompletesTheSubmittedCapture) {
    FakeSlotStorage storage;
    std::unique_ptr<Slot> queued = AllocateOrDie(storage);
    std::unique_ptr<Slot> inline_slot = AllocateOrDie(storage);
    ASSERT_TRUE(queued != nullptr && inline_slot != nullptr);
    Completion queued_done;
    Completion inline_done;

    storage.Capture(*queued, TaggedSurface(1).Source(), queued_done.Callback());
    storage.set_complete_inline(true);
    storage.Capture(*inline_slot, TaggedSurface(2).Source(), inline_done.Callback());

    ASSERT_TRUE(inline_done.result.has_value());
    EXPECT_THAT(inline_done.result->status, IsOk());
    EXPECT_THAT(ReadAll(*inline_slot), Each(2));
    EXPECT_FALSE(queued_done.result.has_value());
    EXPECT_EQ(storage.PendingCaptures(), 1);

    ASSERT_TRUE(storage.CompleteNext());
    ASSERT_TRUE(queued_done.result.has_value());
    EXPECT_THAT(ReadAll(*queued), Each(1));
}

}  // namespace
}  // namespace goldfish::display::testing
