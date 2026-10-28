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

#include "absl/base/no_destructor.h"

#include "goldfish/input/virtio_input_android.h"

namespace goldfish::input {

VirtioInputActiveEventSource& VirtioInputActiveEvents() {
    static absl::NoDestructor<VirtioInputActiveEventSource> events;
    return *events;
}

}  // namespace goldfish::input

extern "C" {

size_t virtio_input_android_add_active_listener(VirtioInputAndroidActiveCallback callback,
                                                void* opaque) {
    if (callback == nullptr) {
        return goldfish::input::VirtioInputActiveEventSource::kInvalidCallbackId;
    }
    return goldfish::input::VirtioInputActiveEvents().AddCallback(
            [callback, opaque](const goldfish::input::VirtioInputActiveEvent& event) {
                callback(event.vhid, event.active_generation, opaque);
            });
}

void virtio_input_android_remove_active_listener(size_t listener_id) {
    goldfish::input::VirtioInputActiveEvents().RemoveCallback(listener_id);
}

void virtio_input_android_notify_active_changed(struct VirtIOInputHID* vhid,
                                                uint64_t active_generation) {
    if (vhid == nullptr) {
        return;
    }
    goldfish::input::VirtioInputActiveEvents().FireEvent(
            goldfish::input::VirtioInputActiveEvent{vhid, active_generation});
}

}  // extern "C"
