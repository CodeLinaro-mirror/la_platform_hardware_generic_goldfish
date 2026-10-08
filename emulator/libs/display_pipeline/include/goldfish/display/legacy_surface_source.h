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

#include <cstddef>
#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/display/frame.h"
#include "goldfish/display/frame_source.h"
#include "goldfish/display/geometry.h"
#include "goldfish/display/slot_pool.h"
#include "goldfish/display/slot_storage.h"

namespace goldfish::display {

/**
 * Synchronous CPU `IFrameSource` that copies a host surface (such as a Pixman
 * image or QEMU `DisplaySurface`) into a bounded `SlotPool`.
 *
 * Architectural Role:
 * Serves as the frame source for CPU-backed displays (`PixmanDisplay`).
 *
 * Concept & Data Flow:
 * - **Hook-Driven Capture:** When `FrameProducer` calls `Capture()`,
 *   `LegacySurfaceSource` invokes `Hooks::capture(*this)`. The display's hook
 *   inspects its current surface and either calls `CaptureFrom()` with a
 *   borrowed `SurfaceSource` view or returns `Start::kNoContent` /
 *   `Start::kNotReady`.
 * - **Starvation & Automatic Recovery:** If `CaptureFrom()` finds all
 *   `slot_count` slots in the current `SlotPool` leased by consumers, it sets
 *   `starved_ = true` and returns `Start::kNoSlot`. As soon as a consumer drops
 *   a `SlotLease` belonging to the *current* pool, `OnSlotFreed()` clears
 *   `starved_` and calls `IFrameProducer::RequestFrame()` (which runs
 *   `Refresh()` and captures the latest surface). Slots returning to an older
 *   pool from before a `Resize()` or `Reset()` are ignored because they cannot
 *   satisfy the current pool's capacity.
 *
 * Thread Safety:
 * `IFrameSource` methods (`OnDemandChanged`, `Refresh`, `Capture`) and
 * `CaptureFrom()` are serialized by `FrameProducer`'s pump. `Reset()`,
 * `Resize()`, and slot-return callbacks (`OnSlotFreed()`) are thread-safe and
 * may be called from any thread.
 *
 * Ownership & Lifetimes:
 * Managed via `std::shared_ptr<LegacySurfaceSource>` so `SlotPool`'s `OnFree`
 * callback can reference it weakly. The owning display must call
 * `FrameProducer::Shutdown()` in its destructor before destroying any state
 * accessed by `Hooks`.
 */
class LegacySurfaceSource final : public IFrameSource,
                                  public std::enable_shared_from_this<LegacySurfaceSource> {
  public:
    /** Display-specific callbacks invoked during producer pump cycles. */
    struct Hooks {
        /** Optional callback invoked by `Refresh()` to update the backing surface before capture.
         */
        absl::AnyInvocable<void()> refresh;
        /** Optional callback invoked by `OnDemandChanged()` when consumer demand transitions. */
        absl::AnyInvocable<void(bool warm)> demand_changed;
        /**
         * Required callback invoked by `Capture()`: inspects the display's
         * surface and either calls `source.CaptureFrom(...)` or returns
         * `Start::kNoContent` / `Start::kNotReady`.
         */
        absl::AnyInvocable<Start(LegacySurfaceSource& source)> capture;
    };

    /**
     * @param producer Producer that receives `RequestFrame()` calls upon
     *        recovering from slot starvation; held weakly.
     * @param hooks Display callbacks for refreshing and reading the surface.
     * @param storage Backend storage (typically `CpuSlotStorage`) that
     *        allocates slots and copies surface pixels into them.
     * @param slot_count Maximum number of slots in the pool (clamped to `>= 1`).
     */
    static std::shared_ptr<LegacySurfaceSource> Create(std::weak_ptr<IFrameProducer> producer,
                                                       Hooks hooks,
                                                       std::unique_ptr<ISlotStorage> storage,
                                                       size_t slot_count = 4);

    LegacySurfaceSource(const LegacySurfaceSource&) = delete;
    LegacySurfaceSource& operator=(const LegacySurfaceSource&) = delete;

    void OnDemandChanged(bool warm) override ABSL_LOCKS_EXCLUDED(pool_mutex_);
    void Refresh() override;
    Start Capture(CaptureCompletion done) override;

    /**
     * Copies `src` (borrowed for the duration of the call) into an idle or
     * newly allocated slot from the pool matching `src.dimensions`.
     *
     * Must only be called from inside `Hooks::capture`. If slot allocation
     * fails, returns `Start::kNoSlot` without marking the pool starved (since
     * releasing an existing slot would not fix an allocation error).
     */
    Start CaptureFrom(const SurfaceSource& src) ABSL_LOCKS_EXCLUDED(pool_mutex_);

    /**
     * Drops the current `SlotPool` and clears any pending starvation debt.
     *
     * Any leased slots from before the reset are freed when their last
     * consumer reference drops, without triggering a re-capture request.
     */
    void Reset() ABSL_LOCKS_EXCLUDED(pool_mutex_);

    /**
     * Notifies the source that the display surface has resized to `size`.
     *
     * If `pool_` had a different size, drops the old pool; if the old pool was
     * starved, immediately calls `RequestFrame()` so a new frame is captured
     * into a fresh pool of `size`.
     */
    void Resize(Dimensions size) ABSL_LOCKS_EXCLUDED(pool_mutex_);

  private:
    LegacySurfaceSource(std::weak_ptr<IFrameProducer> producer, Hooks hooks,
                        std::unique_ptr<ISlotStorage> storage, size_t slot_count);

    // Invoked when a leased slot is returned to `pool`.
    void OnSlotFreed(const SlotPool& pool) ABSL_LOCKS_EXCLUDED(pool_mutex_);
    void RequestFrame() const;

    const std::weak_ptr<IFrameProducer> producer_;
    Hooks hooks_;
    const size_t slot_count_;
    const std::unique_ptr<ISlotStorage> storage_;
    CaptureCompletion done_;  // Completion for the active Capture() call.

    absl::Mutex pool_mutex_;
    // Active slot pool for the current surface dimensions; null until first capture.
    std::shared_ptr<SlotPool> pool_ ABSL_GUARDED_BY(pool_mutex_);
    // True if a capture was skipped because all slots in pool_ were leased.
    bool starved_ ABSL_GUARDED_BY(pool_mutex_) = false;
};

}  // namespace goldfish::display
