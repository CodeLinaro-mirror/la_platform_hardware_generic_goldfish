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

#include <memory>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/perfetto/perfetto_categories.h"

namespace goldfish::display {

std::shared_ptr<LegacySurfaceSource> LegacySurfaceSource::Create(
        std::weak_ptr<IFrameProducer> producer, Hooks hooks, std::unique_ptr<ISlotStorage> storage,
        size_t slot_count) {
    return std::shared_ptr<LegacySurfaceSource>(new LegacySurfaceSource(
            std::move(producer), std::move(hooks), std::move(storage), slot_count));
}

LegacySurfaceSource::LegacySurfaceSource(std::weak_ptr<IFrameProducer> producer, Hooks hooks,
                                         std::unique_ptr<ISlotStorage> storage, size_t slot_count)
        : producer_(std::move(producer))
        , hooks_(std::move(hooks))
        , slot_count_(slot_count > 0 ? slot_count : 1)
        , storage_(std::move(storage)) {}

void LegacySurfaceSource::OnDemandChanged(bool warm) {
    if (hooks_.demand_changed != nullptr) hooks_.demand_changed(warm);
    if (warm) return;
    absl::MutexLock pool_lock(&pool_mutex_);
    starved_ = false;  // A cold source owes no capture.
}

void LegacySurfaceSource::Refresh() {
    if (hooks_.refresh != nullptr) hooks_.refresh();
}

IFrameSource::Start LegacySurfaceSource::Capture(CaptureCompletion done) {
    done_ = std::move(done);
    const Start start = hooks_.capture(*this);
    done_ = CaptureCompletion();  // Unused if no capture started.
    return start;
}

IFrameSource::Start LegacySurfaceSource::CaptureFrom(const SurfaceSource& src) {
    std::shared_ptr<SlotPool> replaced;            // Destroyed after the lock is released.
    std::vector<std::unique_ptr<Slot>> new_slots;  // Destroyed outside the lock on failure.
    PooledSlot slot;
    {
        absl::MutexLock pool_lock(&pool_mutex_);
        if (pool_ == nullptr || pool_->size() != src.dimensions) {
            replaced = std::move(pool_);
            starved_ = false;
            new_slots.reserve(slot_count_);
            for (size_t i = 0; i < slot_count_; ++i) {
                absl::StatusOr<std::unique_ptr<Slot>> allocated =
                        storage_->Allocate(src.dimensions);
                if (!allocated.ok()) {
                    LOG_EVERY_N_SEC(WARNING, 10)
                            << "Cannot allocate a " << src.dimensions.width << "x"
                            << src.dimensions.height << " slot: " << allocated.status();
                    return Start::kNoSlot;  // Not starved: a release would not help.
                }
                new_slots.push_back(*std::move(allocated));
            }
            pool_ = SlotPool::Create(src.dimensions, std::move(new_slots),
                                     [weak = weak_from_this()](const SlotPool& freed) {
                                         if (auto self = weak.lock()) self->OnSlotFreed(freed);
                                     });
        }
        slot = pool_->TryAcquire();  // Never overwrites a leased slot.
        if (slot == nullptr) {
            starved_ = true;  // Every slot is leased; a release requests the capture.
            TRACE_EVENT_INSTANT("display", "LegacySurfaceSource::SlotsExhausted", "slots",
                                slot_count_);
            return Start::kNoSlot;
        }
        starved_ = false;
    }
    Slot& target = *slot;
    storage_->Capture(
            target, src,
            [slot = std::move(slot), done = std::move(done_)](CaptureResult result) mutable {
                if (!result.status.ok()) {
                    LOG_EVERY_N_SEC(WARNING, 10) << "Frame capture failed: " << result.status;
                    slot.reset();  // Back to its pool.
                    std::move(done)(result.status);
                    return;
                }
                std::move(done)(SlotLease(std::move(slot), result.dimensions));
            });
    return Start::kStarted;
}

void LegacySurfaceSource::Reset() {
    std::shared_ptr<SlotPool> dropped;  // Destroyed after the lock is released.
    absl::MutexLock pool_lock(&pool_mutex_);
    dropped = std::move(pool_);  // Its slots, and so its debt, request nothing.
    starved_ = false;
}

void LegacySurfaceSource::Resize(Dimensions size) {
    std::shared_ptr<SlotPool> dropped;  // Destroyed after the lock is released.
    {
        absl::MutexLock pool_lock(&pool_mutex_);
        if (pool_ == nullptr || pool_->size() == size) return;
        dropped = std::move(pool_);
        // The next capture takes a slot of a new pool.
        if (!std::exchange(starved_, false)) return;
    }
    RequestFrame();
}

void LegacySurfaceSource::OnSlotFreed(const SlotPool& pool) {
    {
        absl::MutexLock pool_lock(&pool_mutex_);
        // Only a capture skipped for lack of a slot requests a retry, and a
        // slot of an older pool returned while that pool is being replaced
        // frees nothing a capture on the current pool can use.
        if (!starved_ || &pool != pool_.get()) return;
        starved_ = false;
    }
    RequestFrame();  // Posts; the slot may be freed inside a sink call.
}

void LegacySurfaceSource::RequestFrame() const {
    if (const std::shared_ptr<IFrameProducer> producer = producer_.lock()) {
        producer->RequestFrame();
    }
}

}  // namespace goldfish::display
