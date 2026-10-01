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

// CPU implementation of the frame read pipeline built on libyuv.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "absl/status/status.h"

#include "goldfish/display/frame.h"

namespace goldfish::display {

/**
 * Read-only CPU view of a slot's native 32-bit pixels in `R, G, B, X` memory
 * byte order (`libyuv` "ABGR", `PIXMAN_x8b8g8r8` on little-endian hosts).
 * The fourth byte (`X`) is undefined padding and forced to `0xFF` on RGBA reads.
 */
struct SlotPixels {
    const uint8_t* data = nullptr;
    size_t stride = 0;
    Dimensions dimensions{0, 0};
};

/**
 * Move-only RAII buffer leased from the process-wide read-pipeline scratch pool.
 * Returns its backing memory to the pool upon destruction.
 */
class ScratchBuffer {
  public:
    ScratchBuffer(ScratchBuffer&& other) noexcept;
    ScratchBuffer& operator=(ScratchBuffer&&) = delete;
    ~ScratchBuffer();

    /** Never null for a live lease, even a zero-byte one. */
    uint8_t* data() { return bytes_.get(); }
    const uint8_t* data() const { return bytes_.get(); }

  private:
    // Only the pool creates buffers, so every one returned was counted as a loan.
    friend ScratchBuffer AcquireScratchBuffer(size_t bytes);

    ScratchBuffer(std::unique_ptr<uint8_t[]> bytes, size_t capacity)
            : bytes_(std::move(bytes)), capacity_(capacity) {}

    std::unique_ptr<uint8_t[]> bytes_;
    size_t capacity_ = 0;
};

/**
 * Leases an uninitialized scratch buffer of at least `bytes` bytes from the
 * process-wide adaptive scratch pool. Thread-safe. Under ASAN, the bytes past
 * `bytes` are poisoned for the duration of the lease.
 */
ScratchBuffer AcquireScratchBuffer(size_t bytes);

namespace internal {

/** A snapshot of the process-wide scratch pool, for tests. */
struct ScratchPoolStats {
    size_t on_loan = 0;
    size_t free_count = 0;
    size_t free_bytes = 0;
};

ScratchPoolStats GetScratchPoolStatsForTesting();

}  // namespace internal

/**
 * Executes the CPU read pipeline (`crop -> rotate -> scale -> convert -> write`)
 * from `src` into `dst`.
 *
 * Architectural Role:
 * Provides the reference host-CPU pixel transformation pipeline used by
 * `CpuSlotStorage`.
 *
 * Concept & Data Flow:
 * - **Order Optimization:** Because center-aligned bilinear scaling commutes
 *   with orthogonal 90-degree rotations, downscaling is performed *before*
 *   rotation so `libyuv::ARGBRotate` processes fewer pixels.
 * - **Zero-Allocation Steady State:** Intermediate 32-bit `R, G, B, X` buffers
 *   for scaling and rotation are leased from a process-wide scratch pool that
 *   adapts its retained capacity to peak concurrent demand.
 * - **Row Padding Preservation:** Only the active output bytes of each
 *   destination row are written; any trailing stride padding in `dst` is left
 *   untouched.
 *
 * Thread Safety:
 * Re-entrant and thread-safe; may be called concurrently on any consumer worker
 * thread.
 *
 * Preconditions: `src` and `read` are checked (see below); `dst` is not, and
 * must already be validated against `read.output`, as `Slot::ReadInto()` does.
 *
 * @param src Native 32-bit `R, G, B, X` slot pixels; borrowed for the call.
 * @param read Concrete crop, rotation, output dimensions, and YUV matrix
 *        resolved against `src.dimensions` by `Slot::ReadInto()`.
 * @param dst Validated destination planes; borrowed for the call.
 * @return `OK` on success; `INVALID_ARGUMENT` if `src` has no pixels, its
 *         stride is short or exceeds `INT_MAX`, the crop is empty or outside
 *         `src`, or `read.output` is empty, larger than the rotated crop, or
 *         odd for NV12; `INTERNAL` if a `libyuv` kernel fails.
 */
absl::Status CpuReadPipeline(const SlotPixels& src, const ReadRequest& read, const ReadTarget& dst);

}  // namespace goldfish::display
