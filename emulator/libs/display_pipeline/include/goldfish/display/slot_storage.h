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

// Storage backend contract for allocating frame Slots and capturing host
// surfaces into them.

#include <cstddef>
#include <cstdint>
#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "goldfish/display/frame.h"
#include "goldfish/display/geometry.h"

namespace goldfish::display {

/**
 * Non-owning view of a host-memory guest surface (such as a QEMU
 * `DisplaySurface` or `pixman_image_t`). Borrowed only for the synchronous
 * duration of `ISlotStorage::Capture()`.
 */
struct SurfaceSource {
    const uint8_t* data = nullptr;
    Dimensions dimensions{0, 0};
    /** Row stride in bytes. */
    size_t stride = 0;
    /** Pixel format code (`pixman_format_code_t`). */
    uint32_t pixman_format = 0;
};

/**
 * Result delivered to `ISlotStorage::Capture()`'s completion callback.
 */
struct CaptureResult {
    /** `OK` on success, `CANCELLED` if the capture was aborted, or an error status. */
    absl::Status status;
    /** Native dimensions of the captured frame; slot pixels are stored in `R, G, B, X` order. */
    Dimensions dimensions{0, 0};
};

/**
 * Abstract backend that allocates `Slot` buffers and captures `SurfaceSource`
 * content into them (implemented by `CpuSlotStorage` for host memory).
 *
 * Thread Safety & Lifetimes:
 * Implementations are thread-safe. Allocated `Slot` instances are
 * self-contained and remain readable by consumers even after the originating
 * `ISlotStorage` has been destroyed.
 */
class ISlotStorage {
  public:
    virtual ~ISlotStorage() = default;

    /**
     * Allocates a new `Slot` buffer of `size`.
     *
     * May perform blocking allocation work. Never called while holding a
     * frame source's mutex.
     *
     * @param size Native pixel dimensions of the slot to allocate.
     * @return Newly allocated `Slot`, or an error status if `size` is invalid
     *         or allocation fails.
     */
    virtual absl::StatusOr<std::unique_ptr<Slot>> Allocate(Dimensions size) = 0;

    /**
     * Copies pixel content from `src` into `dst` (which must have been
     * allocated by this storage backend) and invokes `done` at most once when
     * the capture finishes.
     *
     * `src` is borrowed only for the duration of the `Capture()` call. The
     * caller's `done` closure owns the `PooledSlot` referencing `dst`, keeping
     * `dst` alive until `done` is invoked or destroyed. Destroying `done`
     * without invoking it (e.g., when a capture is cancelled on storage
     * teardown) automatically triggers `CaptureCompletion`'s `CANCELLED` path;
     * implementations must never hold internal locks while invoking or
     * destroying `done`.
     *
     * @param dst Destination slot allocated by this storage.
     * @param src Host surface to copy from.
     * @param done One-shot completion callback.
     */
    virtual void Capture(Slot& dst, const SurfaceSource& src,
                         absl::AnyInvocable<void(CaptureResult) &&> done) = 0;
};

}  // namespace goldfish::display
