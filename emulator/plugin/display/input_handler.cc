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

#include "goldfish/display/input_handler.h"

#include <algorithm>

// clang-format off
// IWYU pragma: begin_keep
extern "C" {
#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/surface.h"
#include "qapi/error.h"
#include "qom/object.h"
}
#include "virtio_bridge.h"
// IWYU pragma: end_keep
// clang-format on

namespace goldfish::display {

namespace {

// Event Types
constexpr int kEvSyn = 0x00;
constexpr int kEvAbs = 0x03;

// Synchronization Events
constexpr int kSynReport = 0x00;

// Absolute Axes (Multi-Touch)
constexpr int kAbsMtSlot = 0x2f;        // 47
constexpr int kAbsMtTouchMajor = 0x30;  // 48
constexpr int kAbsMtTouchMinor = 0x31;  // 49
constexpr int kAbsMtPositionX = 0x35;   // 53
constexpr int kAbsMtPositionY = 0x36;   // 54
constexpr int kAbsMtTrackingId = 0x39;  // 57
constexpr int kAbsMtPressure = 0x3a;    // 58

}  // namespace

void InputHandler::SendMultiTouchEvent(VirtIOInputHID* vhid, uint8_t slot, int x, int y,
                                       MultiTouchType type, const Dimensions& dims) {
    if (!vhid) return;

    // 1. Scale Coordinates (0..Width -> 0..32767)
    int abs_x = static_cast<int>(static_cast<int64_t>(x) * 0x7FFF / (dims.width ? dims.width : 1));
    int abs_y =
            static_cast<int>(static_cast<int64_t>(y) * 0x7FFF / (dims.height ? dims.height : 1));

    // Clamp
    abs_x = std::clamp(abs_x, 0, 0x7FFF);
    abs_y = std::clamp(abs_y, 0, 0x7FFF);

    // Select Slot
    virtio_input_send_evdev(vhid, kEvAbs, kAbsMtSlot, slot);

    if (type == MultiTouchType::kBegin || type == MultiTouchType::kUpdate) {
        if (type == MultiTouchType::kBegin) {
            virtio_input_send_evdev(vhid, kEvAbs, kAbsMtTrackingId, slot);
        }
        virtio_input_send_evdev(vhid, kEvAbs, kAbsMtPressure, 0x400);
        virtio_input_send_evdev(vhid, kEvAbs, kAbsMtTouchMajor, 0x500);
        virtio_input_send_evdev(vhid, kEvAbs, kAbsMtTouchMinor, 0x500);

        virtio_input_send_evdev(vhid, kEvAbs, kAbsMtPositionX, abs_x);
        virtio_input_send_evdev(vhid, kEvAbs, kAbsMtPositionY, abs_y);
    } else if (type == MultiTouchType::kEnd || type == MultiTouchType::kCancel) {
        virtio_input_send_evdev(vhid, kEvAbs, kAbsMtPressure, 0);
        virtio_input_send_evdev(vhid, kEvAbs, kAbsMtTrackingId, -1);
    }

    virtio_input_send_evdev(vhid, kEvSyn, kSynReport, 0);
}

void InputHandler::SendMouseEvent(VirtIOInputHID* vhid, int x, int y, int button_mask,
                                  const Dimensions& dims) {
    if (!vhid) return;

    // 1. Scale Coordinates (0..Width -> 0..32767)
    int abs_x = static_cast<int>(static_cast<int64_t>(x) * 0x7FFF / (dims.width ? dims.width : 1));
    int abs_y =
            static_cast<int>(static_cast<int64_t>(y) * 0x7FFF / (dims.height ? dims.height : 1));

    // Clamp
    abs_x = std::clamp(abs_x, 0, 0x7FFF);
    abs_y = std::clamp(abs_y, 0, 0x7FFF);

    const bool is_down = (button_mask & 0x01);  // Left Click

    // Always select Slot 0 (Primary Finger)
    virtio_input_send_evdev(vhid, kEvAbs, kAbsMtSlot, 0);

    if (is_down) {
        // Start tracking (ID 0)
        virtio_input_send_evdev(vhid, kEvAbs, kAbsMtTrackingId, 0);
        virtio_input_send_evdev(vhid, kEvAbs, kAbsMtPressure, 0x400);  // 1024
        virtio_input_send_evdev(vhid, kEvAbs, kAbsMtTouchMajor, 0x500);
        virtio_input_send_evdev(vhid, kEvAbs, kAbsMtTouchMinor, 0x500);

        // We send this on every frame where button is down, even if just moving
        virtio_input_send_evdev(vhid, kEvAbs, kAbsMtPositionX, abs_x);
        virtio_input_send_evdev(vhid, kEvAbs, kAbsMtPositionY, abs_y);

        // Commit
        virtio_input_send_evdev(vhid, kEvSyn, kSynReport, 0);

    } else {
        // TOUCH UP ---
        // Only send this ONCE when the button is released
        virtio_input_send_evdev(vhid, kEvAbs, kAbsMtPressure, 0);
        virtio_input_send_evdev(vhid, kEvAbs, kAbsMtTrackingId, -1);
        virtio_input_send_evdev(vhid, kEvSyn, kSynReport, 0);
    }
}

}  // namespace goldfish::display
