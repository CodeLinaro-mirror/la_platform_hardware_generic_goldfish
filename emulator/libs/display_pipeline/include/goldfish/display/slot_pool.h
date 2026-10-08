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
#pragma once

// Bounded, recycling pool of fixed-size Slots for a single display geometry and
// reset generation.

#include <cstddef>
#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/display/frame.h"
#include "goldfish/display/geometry.h"

namespace goldfish::display {

/**
 * Exclusive, writable handle to a `Slot` acquired from a `SlotPool`.
 *
 * Moving a `PooledSlot` into a `SlotLease` transitions the slot to read-only
 * consumer access. Destroying either the `PooledSlot` (e.g., on capture failure)
 * or the resulting `SlotLease` (when the last consumer drops its `FramePtr`)
 * returns the slot to its originating `SlotPool` if the pool is still alive,
 * or frees the slot immediately if the pool was dropped by a resize or reset.
 */
using PooledSlot = std::unique_ptr<Slot, SlotReturn>;

/**
 * Fixed-capacity pool of recyclable `Slot`s of uniform `Dimensions`.
 *
 * Architectural Role:
 * `SlotPool` enforces a strict upper bound on per-display slot memory while
 * decoupling slot lifetimes from display resize and reset events. Each
 * `SlotPool` instance represents a single geometry/reset generation: when a
 * display resizes or resets, the source simply drops its `std::shared_ptr<SlotPool>`
 * and creates a new one. Idle slots in the old pool are freed immediately, while
 * slots still leased by consumers are automatically freed as soon as their last
 * `SlotLease` is destroyed.
 *
 * Thread Safety:
 * Fully thread-safe. The `on_free` callback is invoked outside `idle_mutex_` on
 * whichever thread returned the slot, and may execute concurrently if multiple
 * leases are dropped on different threads simultaneously.
 */
class SlotPool final : public std::enable_shared_from_this<SlotPool> {
  public:
    /**
     * Invoked without `idle_mutex_` held whenever a slot returns to this pool:
     * when a `SlotLease` dies, and also when a `PooledSlot` is destroyed
     * without being leased (for example after a failed capture). Used by
     * sources to trigger a re-capture when recovering from slot starvation.
     * May drop the last reference to the pool.
     */
    using OnFree = absl::AnyInvocable<void(const SlotPool& pool) const>;

    /**
     * @param size Native pixel dimensions of every slot in this pool.
     * @param slots Pre-allocated, non-null slots of `size` managed by this pool.
     * @param on_free Optional callback invoked whenever a slot returns to `idle_`.
     */
    static std::shared_ptr<SlotPool> Create(Dimensions size,
                                            std::vector<std::unique_ptr<Slot>> slots,
                                            OnFree on_free);

    SlotPool(const SlotPool&) = delete;
    SlotPool& operator=(const SlotPool&) = delete;

    Dimensions size() const { return size_; }
    size_t capacity() const { return capacity_; }

    /**
     * Pops and returns an idle slot from the pool, or `nullptr` if all
     * `capacity()` slots are currently leased.
     */
    PooledSlot TryAcquire() ABSL_LOCKS_EXCLUDED(idle_mutex_);

    /** @return `true` if at least one slot is currently idle (`TryAcquire() != nullptr`). */
    bool HasCapacity() const ABSL_LOCKS_EXCLUDED(idle_mutex_);

  private:
    friend class SlotReturn;

    SlotPool(Dimensions size, std::vector<std::unique_ptr<Slot>> slots, OnFree on_free);

    void Return(std::unique_ptr<Slot> slot) ABSL_LOCKS_EXCLUDED(idle_mutex_);

    const Dimensions size_;
    const size_t capacity_;
    const OnFree on_free_;
    mutable absl::Mutex idle_mutex_;
    std::vector<std::unique_ptr<Slot>> idle_ ABSL_GUARDED_BY(idle_mutex_);
};

}  // namespace goldfish::display
