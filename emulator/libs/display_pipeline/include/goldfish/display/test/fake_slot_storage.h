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
#pragma once

// A scriptable ISlotStorage for tests of frame sources and of producers built
// on them. Captures stay pending until the test completes them, so every
// interleaving of capture, completion, reset and lease release can be driven
// deterministically from one thread.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/display/frame.h"
#include "goldfish/display/geometry.h"
#include "goldfish/display/slot_storage.h"

namespace goldfish::display::testing {

// Counters that outlive the storage (its source owns it).
struct FakeStorageStats {
    absl::Mutex stats_mutex;
    int allocations ABSL_GUARDED_BY(stats_mutex) = 0;
    int live_buffers ABSL_GUARDED_BY(stats_mutex) = 0;
    int captures ABSL_GUARDED_BY(stats_mutex) = 0;
    int reads ABSL_GUARDED_BY(stats_mutex) = 0;

    int Allocations() {
        absl::MutexLock lock(&stats_mutex);
        return allocations;
    }
    int LiveBuffers() {
        absl::MutexLock lock(&stats_mutex);
        return live_buffers;
    }
    int Captures() {
        absl::MutexLock lock(&stats_mutex);
        return captures;
    }
    int Reads() {
        absl::MutexLock lock(&stats_mutex);
        return reads;
    }
};

// The content tag a capture of `src` writes: its first byte, or 0 without
// pixels.
uint32_t ContentTagOf(const SurfaceSource& src);

class FakeSlotStorage : public ISlotStorage {
  public:
    explicit FakeSlotStorage(
            std::shared_ptr<FakeStorageStats> stats = std::make_shared<FakeStorageStats>());

    const std::shared_ptr<FakeStorageStats>& stats() const { return stats_; }

    /**
     * When true, `Capture()` completes the capture it was given inline with
     * `OK` instead of queueing it. Captures already queued stay pending.
     */
    void set_complete_inline(bool complete_inline) ABSL_LOCKS_EXCLUDED(storage_mutex_);

    /**
     * When true, `Capture()` destroys `done` without invoking it, simulating a
     * backend that drops its completion callback on submission failure or
     * teardown.
     */
    void set_drop_completions(bool drop) ABSL_LOCKS_EXCLUDED(storage_mutex_);

    /** Configures the next `Allocate()` call to fail with `status`. */
    void FailNextAllocation(absl::Status status) ABSL_LOCKS_EXCLUDED(storage_mutex_);

    int PendingCaptures() ABSL_LOCKS_EXCLUDED(storage_mutex_);

    // Completes the oldest pending capture, outside the storage's lock. OK
    // stores the source's content tag in the slot.
    // @return False if no capture is pending.
    bool CompleteNext(absl::Status status = absl::OkStatus()) ABSL_LOCKS_EXCLUDED(storage_mutex_);

    // Destroys the oldest pending capture's `done` without calling it.
    // @return False if no capture is pending.
    bool DropNext() ABSL_LOCKS_EXCLUDED(storage_mutex_);

    absl::StatusOr<std::unique_ptr<Slot>> Allocate(Dimensions size) override
            ABSL_LOCKS_EXCLUDED(storage_mutex_);
    void Capture(Slot& dst, const SurfaceSource& src,
                 absl::AnyInvocable<void(CaptureResult) &&> done) override
            ABSL_LOCKS_EXCLUDED(storage_mutex_);

  private:
    struct PendingCapture {
        Slot* dst = nullptr;  // Owned by `done`.
        uint32_t tag = 0;
        absl::AnyInvocable<void(CaptureResult) &&> done;
    };

    // Must be called without `storage_mutex_` held.
    static void Complete(PendingCapture capture, absl::Status status);
    std::optional<PendingCapture> PopPending() ABSL_LOCKS_EXCLUDED(storage_mutex_);

    const std::shared_ptr<FakeStorageStats> stats_;
    absl::Mutex storage_mutex_;
    bool complete_inline_ ABSL_GUARDED_BY(storage_mutex_) = false;
    bool drop_completions_ ABSL_GUARDED_BY(storage_mutex_) = false;
    std::optional<absl::Status> next_allocation_error_ ABSL_GUARDED_BY(storage_mutex_);
    std::deque<PendingCapture> pending_ ABSL_GUARDED_BY(storage_mutex_);
};

}  // namespace goldfish::display::testing
