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

#include "virtio_bridge.h"

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "hw/virtio/virtio-input.h"
#include "hw/virtio/virtio.h"
// IWYU pragma: end_keep
// clang-format on

#include <stdio.h>

#include "android/base/logging/abseil_log_bridge.h"

int dump_virtio_input_hid(const VirtIOInputHID* vhid, char* buf, size_t len) {
    if (!vhid) {
        return snprintf(buf, len, "null");
    }
    return snprintf(buf, len, "{evt_queue: %p, active: %d}", vhid->parent_obj.evt,
                    vhid->parent_obj.active);
}

int find_virtio_device(Object* obj, void* opaque) {
    VirtioDeviceInfo* device = (VirtioDeviceInfo*)opaque;
    // "virtio-input-android" is the dedicated android virtio-input device
    // used to deliver pointer events.
    if (object_dynamic_cast(obj, "virtio-input-android")) {
        VirtIOInputHID* vhid = VIRTIO_INPUT_HID(obj);
        VirtIODevice* vid = VIRTIO_DEVICE(obj);
        ALOGV(1, "Found virtio-input-android:%s display:%s, head:%d (target display:%s, head:%d)",
              vid->name, vhid->display ? vhid->display : "null", vhid->head, device->display,
              device->head);
        if (vhid->head == device->head && strcmp(vhid->display, device->display) == 0) {
            device->vhid = vhid;
            return 1;
        }
    }

    return 0;
}

void virtio_input_send_evdev(VirtIOInputHID* vhid, uint16_t type, uint16_t code, uint32_t value) {
    VirtIOInput* vinput = VIRTIO_INPUT(vhid);
    virtio_input_event event = {
        .type = cpu_to_le16(type), .code = cpu_to_le16(code), .value = cpu_to_le32(value)};
    ALOGV(2, "virtio_input_send_evdev: (%d, %d, %d) to display:%s, head:%d, active:%d, evt:%p",
          type, code, value, vhid->display ? vhid->display : "null", vhid->head,
          vinput ? vinput->active : -1, vinput ? vinput->evt : NULL);
    virtio_input_send(vinput, &event);
}
