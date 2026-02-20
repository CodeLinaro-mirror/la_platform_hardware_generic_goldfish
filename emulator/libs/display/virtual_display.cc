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

#include "goldfish/display/virtual_display.h"

#include <algorithm>
#include <cstring>
#include <iostream>

#include "absl/log/log.h"

#include "goldfish/devices/multidisplay/multidisplay_device.h"

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/surface.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "emulator/libs/display/virtio_bridge.h"
// IWYU pragma: end_keep
// clang-format on
}

namespace goldfish::display {

namespace {

// Event Types
constexpr int EV_SYN = 0x00;
constexpr int EV_KEY = 0x01;
constexpr int EV_REL = 0x02;
constexpr int EV_ABS = 0x03;

// Synchronization Events
constexpr int SYN_REPORT = 0x00;

// Absolute Axes (Standard)
constexpr int ABS_X = 0x00;
constexpr int ABS_Y = 0x01;
constexpr int ABS_Z = 0x02;

// Absolute Axes (Multi-Touch)
constexpr int ABS_MT_SLOT = 0x2f;         // 47
constexpr int ABS_MT_TOUCH_MAJOR = 0x30;  // 48
constexpr int ABS_MT_TOUCH_MINOR = 0x31;  // 49
constexpr int ABS_MT_WIDTH_MAJOR = 0x32;  // 50
constexpr int ABS_MT_WIDTH_MINOR = 0x33;  // 51
constexpr int ABS_MT_ORIENTATION = 0x34;  // 52
constexpr int ABS_MT_POSITION_X = 0x35;   // 53
constexpr int ABS_MT_POSITION_Y = 0x36;   // 54
constexpr int ABS_MT_TOOL_TYPE = 0x37;    // 55
constexpr int ABS_MT_BLOB_ID = 0x38;      // 56
constexpr int ABS_MT_TRACKING_ID = 0x39;  // 57
constexpr int ABS_MT_PRESSURE = 0x3a;     // 58

// Mouse Buttons
constexpr int BTN_LEFT = 0x110;    // 272
constexpr int BTN_RIGHT = 0x111;   // 273
constexpr int BTN_MIDDLE = 0x112;  // 274
constexpr int BTN_SIDE = 0x113;    // 275
constexpr int BTN_EXTRA = 0x114;   // 276

// Touch Buttons
constexpr int BTN_TOUCH = 0x14a;        // 330
constexpr int BTN_TOOL_FINGER = 0x145;  // 325

// ID for "No Finger" (Tracking ID -1)
constexpr int MT_TRACKING_ID_NONE = -1;

}  // namespace

VirtualDisplay::VirtualDisplay(EventLoop* loop, EventLoop* qloop, uint8_t id, uint32_t width,
                               uint32_t height, uint32_t dpi, uint32_t flags)
        : IDisplay(loop, id, width, height), qemu_loop_(qloop) {
    // Initialize our dummy framebuffer with a solid color (e.g., White RGBA)
    // In reality, this memory would be mapped to a virtio-gpu guest buffer.
    size_t bufferSize = static_cast<size_t>(width) * height * 4;
    frame_buffer_.resize(bufferSize, 0xFF);
    goldfish::devices::multidisplay::SendAddDisplay(id, width, height, dpi, flags);
    SimulateGuestFrameUpdate();
    one_second_timer_ = loop->CreateTimer([this] { OneFrameTick(); });
    one_second_timer_->Schedule(absl::ToChronoMilliseconds(absl::Milliseconds(50)),
                                absl::ToChronoMilliseconds(absl::Milliseconds(50)));

    const char* gpu = "gpu0";
    // Head will normally be > 0 for virtual console
    uint32_t head = this->Id();
    // uint32_t head = 0;
    VirtioDeviceInfo deviceInfo{.display = gpu, .head = head};
    Object* objs = object_resolve_path_component(object_get_root(), "machine");
    if (!object_child_foreach_recursive(objs, ::find_virtio_device, &deviceInfo)) {
        LOG(FATAL) << "Unable to find a virtio device for head: " << deviceInfo.head
                   << " attached to display: " << deviceInfo.display;
    }
    fprintf(stderr, "%s %s %d display id %d vhid %p\n", __FILE__, __func__, __LINE__, (int)(head),
            deviceInfo.vhid);
    vhid_ = deviceInfo.vhid;
}

VirtualDisplay::~VirtualDisplay() {
    ::goldfish::devices::multidisplay::SendDelDisplay(display_id_);
    if (one_second_timer_) {
        one_second_timer_->Cancel();
    }
}

std::pair<int, int> VirtualDisplay::ResizeKeepAspectRatio(int desiredWidth, int desiredHeight) {
    return {GetDimensions().width, GetDimensions().height};
}

absl::StatusOr<FrameInfo> VirtualDisplay::GetPixels(PixelFormat fmt, int width, int height,
                                                    ImageRotation rotation, uint8_t* pixel,
                                                    size_t* cPixels) const {
    // 1. Validate inputs
    if (cPixels == nullptr) {
        return absl::InvalidArgumentError("size pointer cannot be null.");
    }

    int bytesPerPixel = 0;
    if (fmt == PixelFormat::kRgba8888) {
        bytesPerPixel = 4;
    } else if (fmt == PixelFormat::kRgb888) {
        bytesPerPixel = 3;
    } else {
        return absl::InvalidArgumentError("Unsupported PixelFormat requested.");
    }

    width = GetDimensions().width;
    height = GetDimensions().height;

    size_t requiredSize = static_cast<size_t>(width) * height * bytesPerPixel;

    if (requiredSize > *cPixels) {
        auto old = *cPixels;
        *cPixels = requiredSize;
        return absl::FailedPreconditionError(
                absl::StrFormat("Buffer too small; need %u bytes, have %u", requiredSize, old));
    }

    // 1. Check if the guest has bound a color buffer yet
    uint32_t cbHandle = goldfish::devices::multidisplay::GetDisplayColorBuffer(this->Id());

    if (cbHandle > 0) {
        goldfish::devices::multidisplay::ReadDisplayColorBuffer(cbHandle,
                                                                (uint8_t*)frame_buffer_.data());
        auto fast_abgr_to_rgb_le = [](const uint8_t* byte_src, uint8_t* dst, int num_pixels) {
            for (int i = 0; i < num_pixels; i++) {
                // Source index: jumps 4 bytes at a time (skip Alpha)
                // Dest index: jumps 3 bytes at a time
                dst[i * 3 + 0] = byte_src[i * 4 + 0];  // R
                dst[i * 3 + 1] = byte_src[i * 4 + 1];  // G
                dst[i * 3 + 2] = byte_src[i * 4 + 2];  // B
                // Skip byte_src[i*4 + 3] (Alpha)
            }
        };

        fast_abgr_to_rgb_le(frame_buffer_.data(), pixel, width * height);
    } else {
        // guest hasn't bound yet
        std::memcpy(pixel, frame_buffer_.data(), requiredSize);
    }
    // 2. Perform the copy (and theoretically, rotation/color conversion)

    // Update the out parameter with the actual number of bytes written
    *cPixels = requiredSize;

    // 3. Return the exact FrameInfo associated with the pixels we just grabbed
    return Seq();
}

void VirtualDisplay::SendMultiTouchEvent(uint8_t slot, int x, int y, MultiTouchType type) {
    // TODO
    VLOG(2) << "[VirtualDisplay " << static_cast<int>(Id()) << "] MultiTouch -> "
            << "Slot: " << static_cast<int>(slot) << ", X: " << x << ", Y: " << y
            << ", Type: " << static_cast<int>(type) << "\n";
}

void VirtualDisplay::SendMouseEvent(int x, int y, int button_mask) {
    absl::MutexLock lock(&send_lock_);
    if (!vhid_) return;

    int width = GetDimensions().width;
    int height = GetDimensions().height;
    VirtIOInputHID* vhid = vhid_;  // Copy the pointer to use inside the lambda

    auto last_bmask = last_bmask_;

    qemu_loop_
            ->Post([this, vhid = vhid_, x, y, button_mask]() {
                // 1. Scale Coordinates (0..Width -> 0..32767)
                int width = GetDimensions().width;
                int height = GetDimensions().height;
                int abs_x = (int)((int64_t)x * 0x7FFF / (width ? width : 1));
                int abs_y = (int)((int64_t)y * 0x7FFF / (height ? height : 1));

                // Clamp
                abs_x = std::clamp(abs_x, 0, 0x7FFF);
                abs_y = std::clamp(abs_y, 0, 0x7FFF);

                bool is_down = (button_mask & 0x01);  // Left Click

                // Always select Slot 0 (Primary Finger)
                virtio_input_send_evdev(vhid, EV_ABS, ABS_MT_SLOT, 0);

                if (is_down) {
                    // Start tracking (ID 0)
                    virtio_input_send_evdev(vhid, EV_ABS, ABS_MT_TRACKING_ID, 0);
                    virtio_input_send_evdev(vhid, EV_ABS, ABS_MT_PRESSURE, 0x400);  // 1024
                    virtio_input_send_evdev(vhid, EV_ABS, ABS_MT_TOUCH_MAJOR, 0x500);
                    virtio_input_send_evdev(vhid, EV_ABS, ABS_MT_TOUCH_MINOR, 0x500);

                    // We send this on every frame where button is down, even if just moving
                    virtio_input_send_evdev(vhid, EV_ABS, ABS_MT_POSITION_X, abs_x);
                    virtio_input_send_evdev(vhid, EV_ABS, ABS_MT_POSITION_Y, abs_y);

                    // Commit
                    virtio_input_send_evdev(vhid, EV_SYN, SYN_REPORT, 0);

                } else {
                    // TOUCH UP ---
                    // Only send this ONCE when the button is released
                    virtio_input_send_evdev(vhid, EV_ABS, ABS_MT_PRESSURE, 0);
                    virtio_input_send_evdev(vhid, EV_ABS, ABS_MT_TRACKING_ID, -1);
                    virtio_input_send_evdev(vhid, EV_SYN, SYN_REPORT, 0);
                }
            })
            .IgnoreError();

    // E. Update State
    last_bmask_ = button_mask;
    VLOG(2) << "[VirtualDisplay " << static_cast<int>(Id()) << "] Mouse -> "
            << "X: " << x << ", Y: " << y << ", Buttons: " << button_mask << "\n";
}

void VirtualDisplay::SendEvDevEvent(uint16_t type, uint16_t code, uint32_t value) {
    absl::MutexLock lock(&send_lock_);
    qemu_loop_
            ->Post([vhid = vhid_, type, code, value] {
                virtio_input_send_evdev(vhid, type, code, value);
            })
            .IgnoreError();

    VLOG(2) << "[VirtualDisplay " << static_cast<int>(Id()) << "] Raw EvDev -> "
            << "Type: " << type << ", Code: " << code << ", Value: " << value << "\n";
}

void VirtualDisplay::SimulateGuestFrameUpdate() {
    // Call the protected method from the IDisplay base class.
    // This increments the sequence counter, updates the timestamp, and fires
    // the event to any listeners on the EventLoop.
    FrameReceived();
}

void VirtualDisplay::OneFrameTick() {
    SimulateGuestFrameUpdate();
}

}  // namespace goldfish::display
