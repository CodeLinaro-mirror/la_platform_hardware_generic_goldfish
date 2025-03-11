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
#include "android/goldfish/display/QemuDisplay.h"

#include <cstddef>
#include <cstdint>

#include "absl/log/log.h"

#include "android/goldfish/display/NullDisplay.h"
#include "qemu/atomic.hpp"

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/surface.h"
#include "pixman.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "android/goldfish/display/virtio-bridge.h"
// IWYU pragma: end_keep
// clang-format on
}

namespace android::goldfish {

SharedDisplay IDisplay::nullDisplay() {
    static auto nullDisplay = std::make_shared<NullDisplay>();
    return nullDisplay;
}

static ::InputMultiTouchType translate_touch_type(MultiTouchType type) {
    switch (type) {
        case MultiTouchType::BEGIN:
            return INPUT_MULTI_TOUCH_TYPE_BEGIN;
        case MultiTouchType::UPDATE:
            return INPUT_MULTI_TOUCH_TYPE_UPDATE;
        case MultiTouchType::END:
            return INPUT_MULTI_TOUCH_TYPE_END;
        case MultiTouchType::CANCEL:
            return INPUT_MULTI_TOUCH_TYPE_CANCEL;
        case MultiTouchType::DATA:
            return INPUT_MULTI_TOUCH_TYPE_DATA;
    }
}

QemuDisplay::QemuDisplay(QemuConsole* console, DisplaySurface* ds, int id)
    : PixmanDisplay(id, ds->image), mConsole(console) {
    if (!mConsole) {
        mConsole = qemu_console_lookup_by_index(0);
        LOG(INFO) << "Display: " << id << " is using the default (0) console";
    }

    const char* gpu = "gpu0";
    VirtioDeviceInfo deviceInfo{.display = gpu, .head = id};
    Object* objs = container_get(object_get_root(), "/machine");
    if (!object_child_foreach_recursive(objs, ::find_virtio_device, &deviceInfo)) {
        LOG(FATAL) << "Unable to find a virtio device for head: " << deviceInfo.head
                   << " attached to display: " << deviceInfo.display;
    }
    mVhid = deviceInfo.vhid;
}

void QemuDisplay::sendMultiTouchEvent(uint8_t slot, int x, int y, MultiTouchType type) {
    Error* error_warn;
    auto ttype = translate_touch_type(type);
    console_handle_touch_event(mConsole, mTouchSlots, slot, mWidth, mHeight, x, y, ttype,
                               &error_warn);
    warn_report_err(error_warn);
}

void QemuDisplay::sendMouseEvent(int x, int y, int button_mask) {
    static uint32_t bmap[INPUT_BUTTON__MAX] = {
            [INPUT_BUTTON_LEFT] = 0x01,       [INPUT_BUTTON_MIDDLE] = 0x02,
            [INPUT_BUTTON_RIGHT] = 0x04,      [INPUT_BUTTON_WHEEL_UP] = 0x08,
            [INPUT_BUTTON_WHEEL_DOWN] = 0x10,
    };

    if (mlast_bmask != button_mask) {
        qemu_input_update_buttons(mConsole, bmap, mlast_bmask, button_mask);
        mlast_bmask = button_mask;
    }

    qemu_input_queue_abs(mConsole, INPUT_AXIS_X, x, 0, mWidth);
    qemu_input_queue_abs(mConsole, INPUT_AXIS_Y, y, 0, mHeight);
    qemu_input_event_sync();
}

void QemuDisplay::sendEvDevEvent(uint16_t type, uint16_t code, uint32_t value) {
    virtio_input_send_evdev(mVhid, type, code, value);
}

}  // namespace android::goldfish