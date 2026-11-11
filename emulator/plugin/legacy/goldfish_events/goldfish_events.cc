// Copyright (C) 2026 The Android Open Source Project
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

#include "goldfish/legacy/goldfish_events.h"

#include <atomic>
#include <cstdint>

#include "goldfish/input/virtio_input_android.h"

namespace goldfish::legacy {
namespace {

std::atomic<struct ::VirtIOInputHID*> g_primary_touch_device{nullptr};
std::atomic<GoldfishEventsSendFn> g_send_handler{nullptr};
std::atomic<bool> g_active{false};
std::atomic<uint64_t> g_active_generation{0};

}  // namespace
}  // namespace goldfish::legacy

extern "C" {

void goldfish_events_set_primary_touch_device(struct VirtIOInputHID* device) {
    goldfish::legacy::g_primary_touch_device.store(device, std::memory_order_release);
}

struct VirtIOInputHID* goldfish_events_get_primary_touch_device(void) {
    return goldfish::legacy::g_primary_touch_device.load(std::memory_order_acquire);
}

void goldfish_events_set_send_handler(GoldfishEventsSendFn handler) {
    goldfish::legacy::g_send_handler.store(handler, std::memory_order_release);
}

void goldfish_events_notify_active_changed(bool active, uint64_t active_generation) {
    // Store generation before `active` so readers observing `active == true`
    // always see its matching generation.
    goldfish::legacy::g_active_generation.store(active_generation, std::memory_order_release);
    goldfish::legacy::g_active.store(active, std::memory_order_release);
    if (struct VirtIOInputHID* device =
                goldfish::legacy::g_primary_touch_device.load(std::memory_order_acquire)) {
        virtio_input_android_notify_active_changed(device, active_generation);
    }
}

bool goldfish_events_is_active(void) {
    return goldfish::legacy::g_active.load(std::memory_order_acquire);
}

uint64_t goldfish_events_get_active_generation(void) {
    return goldfish::legacy::g_active_generation.load(std::memory_order_acquire);
}

int goldfish_events_send(int type, int code, int value) {
    GoldfishEventsSendFn handler = goldfish::legacy::g_send_handler.load(std::memory_order_acquire);
    if (handler == nullptr) {
        return -1;
    }
    return handler(type, code, value);
}

}  // extern "C"
