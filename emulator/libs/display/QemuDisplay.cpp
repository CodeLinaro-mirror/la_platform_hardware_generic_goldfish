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

#include "QemuDisplay.h"

#include <cstddef>
#include <cstdint>

#include "absl/log/log.h"

#include "NullDisplay.h"

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/surface.h"
#include "pixman.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "virtio-bridge.h"
// IWYU pragma: end_keep
// clang-format on
}

namespace goldfish::display {

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

QemuDisplay::QemuDisplay(EventLoop* loop, EventLoop* qemuLoop, QemuConsole* con, DisplaySurface* ds,
                         int index)
        : PixmanDisplay(loop, index, ds->image), mConsole(con), mQemuLoop(qemuLoop) {
    if (!mConsole) {
        LOG(FATAL) << "Display: " << index << " has nullptr console";
    }

    const char* gpu = "gpu0";
    // Head will normally be 0 if this is the default console.
    uint32_t head = qemu_console_get_head(mConsole);
    VirtioDeviceInfo deviceInfo{.display = gpu, .head = head};
    Object* objs = object_resolve_path_component(object_get_root(), "machine");
    if (!object_child_foreach_recursive(objs, ::find_virtio_device, &deviceInfo)) {
        LOG(FATAL) << "Unable to find a virtio device for head: " << deviceInfo.head
                   << " attached to display: " << deviceInfo.display;
    }
    mVhid = deviceInfo.vhid;
}

void QemuDisplay::sendMultiTouchEvent(uint8_t slot, int x, int y, MultiTouchType type) {
    absl::MutexLock lock(&mSendLock);
    VLOG(1) << *this << ", sendMultiTouchEvent(" << slot << ", " << x << ", " << y << ", "
            << (int)type << ")";
    Error* error_warn;
    auto ttype = translate_touch_type(type);
    console_handle_touch_event(mConsole, mTouchSlots, slot, mWidth, mHeight, x, y, ttype,
                               &error_warn);
    warn_report_err(error_warn);
}

static uint32_t bmap[INPUT_BUTTON__MAX] = {
    [INPUT_BUTTON_LEFT] = 0x01,     [INPUT_BUTTON_MIDDLE] = 0x02,     [INPUT_BUTTON_RIGHT] = 0x04,
    [INPUT_BUTTON_WHEEL_UP] = 0x08, [INPUT_BUTTON_WHEEL_DOWN] = 0x10,
};

void QemuDisplay::sendMouseEvent(int x, int y, int button_mask) {
    absl::MutexLock lock(&mSendLock);
    VLOG(2) << *this << ", sendMouseEvent(" << x << ", " << y << ", " << button_mask << ")";
    (void)mQemuLoop->post([con = mConsole, x, y, w = mWidth, h = mHeight, last = mlast_bmask,
                           mask = button_mask] {
        if (last != mask) {
            qemu_input_update_buttons(con, bmap, last, mask);
        }
        qemu_input_queue_abs(con, INPUT_AXIS_X, x, 0, w);
        qemu_input_queue_abs(con, INPUT_AXIS_Y, y, 0, h);
        qemu_input_event_sync();
    });
    mlast_bmask = button_mask;
}

void QemuDisplay::sendEvDevEvent(uint16_t type, uint16_t code, uint32_t value) {
    absl::MutexLock lock(&mSendLock);
    VLOG(1) << *this << ", sendEvDevEvent(" << type << ", " << code << ", " << value << ")";
    (void)mQemuLoop->post([vhid = mVhid, type, code, value] {
        virtio_input_send_evdev(vhid, type, code, value);
    });
}

}  // namespace goldfish::display
