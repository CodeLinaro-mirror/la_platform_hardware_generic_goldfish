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

#include "virtio-bridge.h"

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "hw/virtio/virtio-input.h"
#include "hw/virtio/virtio.h"
// IWYU pragma: end_keep
// clang-format on

#include "android/base/logging/AbseilLogBridge.h"

int find_virtio_device(Object* obj, void* opaque) {
    VirtioDeviceInfo* device = (VirtioDeviceInfo*)opaque;

    if (object_dynamic_cast(obj, TYPE_VIRTIO_INPUT_HID)) {
        VirtIOInputHID* vhid = VIRTIO_INPUT_HID(obj);
        VirtIODevice* vid = VIRTIO_DEVICE(obj);
        ALOGV(1, "Found virtio input:%s display:%s, head:%d", vid->name, vhid->display, vhid->head);
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
    ALOGV(1, "Sending generic evdev event (%d, %d, %d) to display:%s, head:%d", type, code, value,
          vhid->display, vhid->head);
    virtio_input_send(vinput, &event);
}