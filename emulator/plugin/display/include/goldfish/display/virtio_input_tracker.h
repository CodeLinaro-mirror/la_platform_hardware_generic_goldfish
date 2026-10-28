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

#include <atomic>
#include <cstdint>
#include <memory>

#include "goldfish/display/display.h"
#include "goldfish/input/virtio_input_android.h"

namespace goldfish::display {

/**
 * Tracks a display's `VirtIOInputHID` active generation using the hybrid push +
 * QOM pull contract:
 *
 * - Subscribes to `goldfish::input::VirtioInputActiveEvents()` on construction
 *   (safe on non-BQL threads such as gRPC `VirtualDisplay::Create`), updating
 *   an atomic cache and forwarding transitions to the owning `IDisplay`'s
 *   `InputActiveEventSource`.
 * - Lazily seeds the cache from QOM (`virtio_input_hid_active_generation`) on
 *   the first `GenerationOnLoop()` read on `qemu_loop_` (under the BQL) if no
 *   push event arrived yet, so subsequent reads on 240 Hz pointer moves are a
 *   single atomic load with zero QOM lookup overhead.
 */
class VirtioInputActiveTracker {
  public:
    VirtioInputActiveTracker(::VirtIOInputHID* vhid, std::weak_ptr<InputActiveEventSource> sink);
    ~VirtioInputActiveTracker();

    VirtioInputActiveTracker(const VirtioInputActiveTracker&) = delete;
    VirtioInputActiveTracker& operator=(const VirtioInputActiveTracker&) = delete;

    /**
     * Returns the current active generation on the QEMU event loop (under the
     * BQL), or 0 if `vhid` is null.
     */
    uint64_t GenerationOnLoop() const;

    /**
     * Unregisters from `VirtioInputActiveEvents()`, blocking until any
     * in-flight callback on another thread completes. Idempotent.
     */
    void Reset();

  private:
    static constexpr uint64_t kUncached = UINT64_MAX;

    ::VirtIOInputHID* const vhid_;
    mutable std::atomic<uint64_t> cached_generation_;
    goldfish::input::VirtioInputActiveEventSource::ScopedCallbackHandle subscription_;
};

}  // namespace goldfish::display
