// Copyright (C) 2025 The Android Open Source Project
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

#include <stddef.h>
#include <stdint.h>
#include <sys/cdefs.h>

/**
 * Name of the read-only uint64 QOM property of a virtio-input-android device
 * that changes each time its guest driver goes active or inactive (driver
 * ready, driver or device reset, re-probe, snapshot load). Read it, under
 * the BQL, for QMP/test introspection or initial state seeding.
 */
#define VIRTIO_INPUT_ANDROID_ACTIVE_GENERATION "active-generation"

__BEGIN_DECLS
struct VirtIOInputHID;

/**
 * Callback invoked synchronously when a virtio-input-android device advances
 * its active generation in `change_active` (on a vCPU/QEMU thread under the
 * BQL). Implementations must be non-blocking and must not unregister
 * themselves from inside the callback.
 */
typedef void (*VirtioInputAndroidActiveCallback)(struct VirtIOInputHID* vhid,
                                                 uint64_t active_generation, void* opaque);

void virtio_input_android_register_types(void);

/**
 * Registers a listener notified whenever any virtio-input-android device
 * changes active state. Returns a non-zero listener ID, or 0 if `callback` is
 * NULL. Thread-safe.
 */
size_t virtio_input_android_add_active_listener(VirtioInputAndroidActiveCallback callback,
                                                void* opaque);

/**
 * Unregisters a listener previously added by
 * `virtio_input_android_add_active_listener`, blocking until any in-flight
 * invocation on another thread finishes. Passing 0 is a no-op. Thread-safe.
 */
void virtio_input_android_remove_active_listener(size_t listener_id);

/**
 * Dispatches an active-generation change notification for `vhid` to all
 * registered C and C++ listeners. Ignored if `vhid` is NULL.
 */
void virtio_input_android_notify_active_changed(struct VirtIOInputHID* vhid,
                                                uint64_t active_generation);
__END_DECLS

#ifdef __cplusplus
#include "goldfish/eventing/event_sources.h"

namespace goldfish::input {

/**
 * Payload published on `VirtioInputActiveEvents()` whenever a
 * `virtio-input-android` device changes active state.
 */
struct VirtioInputActiveEvent {
    struct ::VirtIOInputHID* vhid = nullptr;
    uint64_t active_generation = 0;
};

using VirtioInputActiveEventSource =
        ::android::base::eventing::WithCallbacks<::android::base::eventing::EventSource<
                VirtioInputActiveEvent,
                ::android::base::eventing::HybridStoragePolicy<
                        VirtioInputActiveEvent, 16,
                        std::shared_ptr<::android::base::eventing::EventListener<
                                VirtioInputActiveEvent>>>>>;

/**
 * Process-wide thread-safe event source fired whenever a
 * `virtio-input-android` device advances its active generation.
 */
VirtioInputActiveEventSource& VirtioInputActiveEvents();

}  // namespace goldfish::input
#endif
