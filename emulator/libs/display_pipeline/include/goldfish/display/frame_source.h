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

// Source contract through which a FrameProducer acquires captured frames from a
// display backend.

#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"

#include "goldfish/display/frame.h"

namespace goldfish::display {

/**
 * Move-only, one-shot completion callback for a single `IFrameSource::Capture`
 * invocation.
 *
 * Architectural Role:
 * Guarantees that every started capture reports its completion to
 * `FrameProducer` exactly once. If a source destroys a `CaptureCompletion`
 * without invoking it (for example, when a capture submission fails or the
 * source is shut down mid-capture), the destructor automatically invokes the
 * callback with `absl::CancelledError` so the producer's in-flight capture
 * state can never be wedged.
 *
 * Thread Safety:
 * May be invoked or destroyed on any thread (either inline during `Capture()`
 * for synchronous sources or asynchronously on a worker thread for
 * asynchronous sources).
 */
class CaptureCompletion final {
  public:
    /**
     * Receives either a complete, immutable `SlotLease` ready for publication,
     * `CANCELLED` if the capture was dropped, or an error status.
     */
    using Callback = absl::AnyInvocable<void(absl::StatusOr<SlotLease>) &&>;

    CaptureCompletion() = default;
    explicit CaptureCompletion(Callback callback) : callback_(std::move(callback)) {}
    /** Leaves `other` empty, so only this one invokes the callback. */
    CaptureCompletion(CaptureCompletion&& other) noexcept
            : callback_(std::exchange(other.callback_, nullptr)) {}
    /** Cancels any previously held uninvoked callback before taking ownership of `other`. */
    CaptureCompletion& operator=(CaptureCompletion&& other) noexcept;
    ~CaptureCompletion();

    CaptureCompletion(const CaptureCompletion&) = delete;
    CaptureCompletion& operator=(const CaptureCompletion&) = delete;

    void operator()(absl::StatusOr<SlotLease> frame) &&;

  private:
    Callback callback_;
};

/**
 * Abstract pixel source driven by a `FrameProducer`'s pump.
 *
 * Architectural Role:
 * Encapsulates how a display's current guest pixels are inspected and copied
 * into a pooled `Slot` (e.g., `LegacySurfaceSource` for synchronous CPU
 * surfaces).
 *
 * Thread Safety & Lifetimes:
 * `FrameProducer` serializes all calls to `OnDemandChanged()`, `Refresh()`, and
 * `Capture()` so at most one source method executes at any time, and guarantees
 * no calls begin after `FrameProducer::Shutdown()` returns.
 */
class IFrameSource {
  public:
    /** Outcome of attempting to start a capture in `Capture()`. */
    enum class Start {
        /** Capture started; `done` will be invoked (or was already invoked inline). */
        kStarted,
        /** All slots in the pool are currently leased; the source will call `RequestFrame()` when a
           slot returns. */
        kNoSlot,
        /** Source is temporarily not ready to capture (e.g., slots still allocating). */
        kNotReady,
        /** Display has no guest content to show; the producer should call
           `IFrameSink::PublishNoContent()`. */
        kNoContent,
    };

    virtual ~IFrameSource() = default;

    /**
     * Notifies the source that consumer demand changed via
     * `IFrameProducer::SetDemand()`.
     *
     * Calls strictly alternate, starting with `true`: demand that flips and
     * flips back before the executor applies it is not reported. When `warm`
     * is `false`, the source is cold and may clear any pending starvation debt.
     */
    virtual void OnDemandChanged(bool /*warm*/) {}

    /**
     * Brings the source's backing surface up to date before an explicit
     * `RequestFrame()` capture (e.g., invoking QEMU's `graphic_hw_update`).
     *
     * Not called for guest-initiated `FrameProducer::Invalidate()` flushes,
     * where the guest has just updated the surface. `Refresh()` may re-enter
     * `FrameProducer::Invalidate()`, which simply marks the producer dirty.
     */
    virtual void Refresh() {}

    /**
     * Initiates a capture of the source's current content into a slot.
     *
     * @param done Completion callback to invoke when the capture finishes. If
     *        `Capture()` returns anything other than `Start::kStarted`, `done`
     *        must be dropped uninvoked.
     * @return Whether the capture started or why it could not start.
     */
    virtual Start Capture(CaptureCompletion done) = 0;
};

}  // namespace goldfish::display
