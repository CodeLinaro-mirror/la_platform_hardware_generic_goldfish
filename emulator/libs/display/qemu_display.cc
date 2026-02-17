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

#include <cstddef>
#include <cstdint>

#include "absl/log/log.h"

#include "emulator/libs/display/NullDisplay.h"
#include "emulator/libs/display/QemuDisplay.h"

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/surface.h"
#include "pixman.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "emulator/libs/display/virtio_bridge.h"
// IWYU pragma: end_keep
// clang-format on
}

namespace goldfish::display {

SharedDisplay IDisplay::GetNullDisplay() {
    static auto null_display = std::make_shared<NullDisplay>();
    return null_display;
}

namespace {

::InputMultiTouchType TranslateTouchType(MultiTouchType type) {
    switch (type) {
    case MultiTouchType::kBegin:
        return INPUT_MULTI_TOUCH_TYPE_BEGIN;
    case MultiTouchType::kUpdate:
        return INPUT_MULTI_TOUCH_TYPE_UPDATE;
    case MultiTouchType::kEnd:
        return INPUT_MULTI_TOUCH_TYPE_END;
    case MultiTouchType::kCancel:
        return INPUT_MULTI_TOUCH_TYPE_CANCEL;
    case MultiTouchType::kData:
        return INPUT_MULTI_TOUCH_TYPE_DATA;
    }
}

uint32_t bmap[INPUT_BUTTON__MAX] = {
    [INPUT_BUTTON_LEFT] = 0x01,     [INPUT_BUTTON_MIDDLE] = 0x02,     [INPUT_BUTTON_RIGHT] = 0x04,
    [INPUT_BUTTON_WHEEL_UP] = 0x08, [INPUT_BUTTON_WHEEL_DOWN] = 0x10,
};

}  // namespace

QemuDisplay::QemuDisplay(EventLoop* loop, EventLoop* qemu_loop, QemuConsole* con,
                         DisplaySurface* ds, int index)
        : PixmanDisplay(loop, index, ds->image), console_(con), qemu_loop_(qemu_loop) {
    if (!console_) {
        LOG(FATAL) << "Display: " << index << " has nullptr console";
    }

    const char* gpu = "gpu0";
    // Head will normally be 0 if this is the default console.
    const uint32_t head = qemu_console_get_head(console_);
    VirtioDeviceInfo device_info{.display = gpu, .head = head};
    Object* objs = object_resolve_path_component(object_get_root(), "machine");
    if (!object_child_foreach_recursive(objs, ::find_virtio_device, &device_info)) {
        LOG(FATAL) << "Unable to find a virtio device for head: " << device_info.head
                   << " attached to display: " << device_info.display;
    }
    vhid_ = device_info.vhid;
}

void QemuDisplay::SendMultiTouchEvent(uint8_t slot, int x, int y, MultiTouchType type) {
    const absl::MutexLock lock(&send_lock_);
    const Dimensions dims = GetDimensions();
    VLOG(1) << *this << ", SendMultiTouchEvent(" << static_cast<int>(slot) << ", " << x << ", " << y
            << ", " << static_cast<int>(type) << ")";
    Error* error_warn;
    auto ttype = TranslateTouchType(type);
    console_handle_touch_event(console_, touch_slots_, slot, static_cast<int>(dims.width),
                               static_cast<int>(dims.height), x, y, ttype, &error_warn);
    warn_report_err(error_warn);
}

void QemuDisplay::SendMouseEvent(int x, int y, int button_mask) {
    const absl::MutexLock lock(&send_lock_);
    const Dimensions dims = GetDimensions();
    VLOG(2) << *this << ", SendMouseEvent(" << x << ", " << y << ", " << button_mask << ")";
    qemu_loop_
            ->Post([con = console_, x, y, w = static_cast<int>(dims.width),
                    h = static_cast<int>(dims.height), last = mlast_bmask_, mask = button_mask] {
                if (last != mask) {
                    qemu_input_update_buttons(con, bmap, last, mask);
                }
                qemu_input_queue_abs(con, INPUT_AXIS_X, x, 0, w);
                qemu_input_queue_abs(con, INPUT_AXIS_Y, y, 0, h);
                qemu_input_event_sync();
            })
            .IgnoreError();
    mlast_bmask_ = button_mask;
}

void QemuDisplay::SendEvDevEvent(uint16_t type, uint16_t code, uint32_t value) {
    const absl::MutexLock lock(&send_lock_);
    VLOG(1) << *this << ", SendEvDevEvent(" << type << ", " << code << ", " << value << ")";
    qemu_loop_
            ->Post([vhid = vhid_, type, code, value] {
                virtio_input_send_evdev(vhid, type, code, value);
            })
            .IgnoreError();
}

}  // namespace goldfish::display
