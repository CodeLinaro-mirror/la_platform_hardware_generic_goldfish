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

// Host-memory ISlotStorage backend for capturing CPU surfaces and reading them
// via CpuReadPipeline.

#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "goldfish/display/frame.h"
#include "goldfish/display/geometry.h"
#include "goldfish/display/slot_storage.h"

namespace goldfish::display {

/**
 * Stateless, host-memory `ISlotStorage` implementation.
 *
 * Architectural Role:
 * Allocates 64-byte-aligned host `R, G, B, X` slot buffers (`PIXMAN_x8b8g8r8`
 * on little-endian hosts) suitable for `libyuv`'s widest SIMD loads, copies
 * `SurfaceSource` images into them synchronously during `Capture()`, and
 * executes consumer reads via `CpuReadPipeline`.
 *
 * Thread Safety:
 * Stateless and fully thread-safe.
 */
class CpuSlotStorage : public ISlotStorage {
  public:
    /**
     * Allocates a 64-byte row-aligned host memory `Slot` of `size`.
     *
     * @param size Native pixel dimensions of the slot.
     * @return Allocated `Slot`, `INVALID_ARGUMENT` if `size` is empty or
     *         exceeds `INT_MAX` stride/byte limits, or `RESOURCE_EXHAUSTED` on
     *         out-of-memory.
     */
    absl::StatusOr<std::unique_ptr<Slot>> Allocate(Dimensions size) override;

    /**
     * Synchronously copies a `SurfaceSource` into `dst` and invokes `done`
     * inline before returning.
     *
     * Fast paths:
     * - Native `R, G, B, X` surfaces (`PIXMAN_x8b8g8r8` / `PIXMAN_a8b8g8r8`)
     *   are copied row-by-row with `std::memcpy`.
     * - `B, G, R, X` surfaces (`PIXMAN_x8r8g8b8` / `PIXMAN_a8r8g8b8`) are
     *   swizzled via `libyuv::ARGBToABGR`.
     * - Other RGB formats pixman supports (channel types ARGB, ABGR, BGRA,
     *   RGBA, such as `PIXMAN_r5g6b5`) are converted via
     *   `pixman_image_composite32`; the surface must be 4-byte aligned.
     *
     * Delivers `INVALID_ARGUMENT` to `done` if `src.dimensions != dst.size()`,
     * the stride is short or exceeds `INT_MAX`, or the format (indexed, gray,
     * YUV, alpha-only, sRGB, float) or alignment is unsupported.
     */
    void Capture(Slot& dst, const SurfaceSource& src,
                 absl::AnyInvocable<void(CaptureResult) &&> done) override;
};

}  // namespace goldfish::display
