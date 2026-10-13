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

#include <memory>
#include <utility>
#include <vector>

#include "absl/log/check.h"

namespace goldfish::display {

std::shared_ptr<SlotPool> SlotPool::Create(Dimensions size,
                                           std::vector<std::unique_ptr<Slot>> slots,
                                           OnFree on_free) {
    for (const std::unique_ptr<Slot>& slot : slots) {
        DCHECK(slot != nullptr) << "SlotPool slots must not be null";
        DCHECK(slot->size() == size) << "SlotPool slots must all have the pool's size";
    }
    return std::shared_ptr<SlotPool>(new SlotPool(size, std::move(slots), std::move(on_free)));
}

SlotPool::SlotPool(Dimensions size, std::vector<std::unique_ptr<Slot>> slots, OnFree on_free)
        : size_(size)
        , capacity_(slots.size())
        , on_free_(std::move(on_free))
        , idle_(std::move(slots)) {}

PooledSlot SlotPool::TryAcquire() {
    std::unique_ptr<Slot> slot;
    {
        absl::MutexLock idle_lock(&idle_mutex_);
        if (idle_.empty()) return nullptr;
        slot = std::move(idle_.back());
        idle_.pop_back();
    }
    return PooledSlot(slot.release(), SlotReturn(weak_from_this()));
}

bool SlotPool::HasCapacity() const {
    absl::MutexLock idle_lock(&idle_mutex_);
    return !idle_.empty();
}

void SlotReturn::operator()(const Slot* slot) const {
    // Slots are acquired writable from the pool and exposed as `const Slot`
    // while leased to consumers; restoring mutability on return is safe
    // because the last lease reference has just been dropped.
    std::unique_ptr<Slot> owned(const_cast<Slot*>(slot));
    if (const std::shared_ptr<SlotPool> pool = pool_.lock()) pool->Return(std::move(owned));
}

void SlotPool::Return(std::unique_ptr<Slot> slot) {
    {
        absl::MutexLock idle_lock(&idle_mutex_);
        idle_.push_back(std::move(slot));
    }
    if (on_free_) on_free_(*this);
}

}  // namespace goldfish::display
