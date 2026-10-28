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

#include "goldfish/display/virtio_input_tracker.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <utility>

#include "virtio_bridge.h"

namespace goldfish::display {

VirtioInputActiveTracker::VirtioInputActiveTracker(::VirtIOInputHID* vhid,
                                                   std::weak_ptr<InputActiveEventSource> sink)
        : vhid_(vhid), cached_generation_(vhid != nullptr ? kUncached : 0) {
    if (vhid_ == nullptr) {
        return;
    }
    subscription_ = ::android::base::eventing::MakeScopedCallback(
            goldfish::input::VirtioInputActiveEvents(),
            [this,
             weak_sink = std::move(sink)](const goldfish::input::VirtioInputActiveEvent& event) {
                if (event.vhid != vhid_) {
                    return;
                }
                cached_generation_.store(event.active_generation, std::memory_order_release);
                if (auto locked_sink = weak_sink.lock()) {
                    locked_sink->FireEvent(event.active_generation);
                }
            });
}

VirtioInputActiveTracker::~VirtioInputActiveTracker() {
    Reset();
}

uint64_t VirtioInputActiveTracker::GenerationOnLoop() const {
    const uint64_t cached = cached_generation_.load(std::memory_order_acquire);
    if (cached != kUncached) {
        return cached;
    }
    const uint64_t seeded = virtio_input_hid_active_generation(vhid_);
    uint64_t expected = kUncached;
    if (cached_generation_.compare_exchange_strong(expected, seeded, std::memory_order_acq_rel,
                                                   std::memory_order_acquire)) {
        return seeded;
    }
    return expected;
}

void VirtioInputActiveTracker::Reset() {
    subscription_.reset();
}

}  // namespace goldfish::display
