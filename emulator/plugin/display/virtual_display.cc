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
#include "goldfish/display/input_handler.h"

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

VirtualDisplay::VirtualDisplay(EventLoop* loop, EventLoop* qloop, uint8_t id, uint32_t width,
                               uint32_t height, uint32_t dpi, uint32_t flags)
        : IDisplay(loop, id, width, height, dpi, flags), qemu_loop_(qloop) {
    // Initialize our dummy framebuffer with a solid color (e.g., White RGBA)
    // In reality, this memory would be mapped to a virtio-gpu guest buffer.
    const size_t buffer_size = static_cast<size_t>(width) * height * 4;
    frame_buffer_.resize(buffer_size, 0xFF);
    goldfish::devices::multidisplay::SendAddDisplay(id, width, height, dpi, flags);
    SimulateGuestFrameUpdate();
    one_second_timer_ = loop->CreateTimer([this] {
        OneFrameTick();
        return true;
    });
    one_second_timer_->Schedule(absl::Milliseconds(50), absl::Milliseconds(50));

    const char* gpu = "gpu0";
    // Head will normally be > 0 for virtual console
    const uint32_t head = this->Id();
    // uint32_t head = 0;
    VirtioDeviceInfo device_info{.display = gpu, .head = head};
    Object* objs = object_resolve_path_component(object_get_root(), "machine");
    if (!object_child_foreach_recursive(objs, ::find_virtio_device, &device_info)) {
        LOG(FATAL) << "Unable to find a virtio device for head: " << device_info.head
                   << " attached to display: " << device_info.display;
    }
    VLOG(1) << "display id " << static_cast<int>(head) << " vhid " << device_info.vhid;
    vhid_ = device_info.vhid;
}

void VirtualDisplay::Disconnect() {
    if (!disconnected_) {
        ::goldfish::devices::multidisplay::SendDelDisplay(display_id_);
        disconnected_ = true;
    }
}

VirtualDisplay::~VirtualDisplay() {
    // WARNING: Do NOT trigger active network operations (like SendDelDisplay)
    // from this C++ destructor. Lingering std::shared_ptr references held by UI
    // windows or background queues can cause this destructor to run asynchronously
    // on non-network threads or post-unrealize when the AVD universe is offline.
    // Intentional network teardown must execute explicitly via Disconnect() instead.
    if (one_second_timer_) {
        one_second_timer_->Cancel();
    }
}

std::pair<int, int> VirtualDisplay::ResizeKeepAspectRatio(int /*desired_width*/,
                                                          int /*desired_height*/) {
    return {GetDimensions().width, GetDimensions().height};
}

absl::StatusOr<FrameInfo> VirtualDisplay::GetPixels(PixelFormat fmt, int width, int height,
                                                    ImageRotation /*rotation*/, uint8_t* pixel,
                                                    size_t* c_pixels) const {
    // 1. Validate inputs
    if (c_pixels == nullptr) {
        return absl::InvalidArgumentError("size pointer cannot be null.");
    }

    int bytes_per_pixel = 0;
    if (fmt == PixelFormat::kRgba8888) {
        bytes_per_pixel = 4;
    } else if (fmt == PixelFormat::kRgb888) {
        bytes_per_pixel = 3;
    } else {
        return absl::InvalidArgumentError("Unsupported PixelFormat requested.");
    }

    width = static_cast<int>(GetDimensions().width);
    height = static_cast<int>(GetDimensions().height);

    const size_t required_size = static_cast<size_t>(width) * height * bytes_per_pixel;

    if (required_size > *c_pixels) {
        auto old = *c_pixels;
        *c_pixels = required_size;
        return absl::FailedPreconditionError(
                absl::StrFormat("Buffer too small; need %u bytes, have %u", required_size, old));
    }

    // 1. Check if the guest has bound a color buffer yet
    const uint32_t cb_handle = goldfish::devices::multidisplay::GetDisplayColorBuffer(this->Id());

    if (pixel != nullptr) {
        if (cb_handle > 0) {
            goldfish::devices::multidisplay::ReadDisplayColorBuffer(
                    cb_handle, const_cast<uint8_t*>(frame_buffer_.data()));
            auto fast_abgr_to_rgb_le = [](const uint8_t* byte_src, uint8_t* dst, int num_pixels) {
                for (int i = 0; i < num_pixels; i++) {
                    // Source index: jumps 4 bytes at a time (skip Alpha)
                    // Dest index: jumps 3 bytes at a time
                    dst[(i * 3) + 0] = byte_src[(i * 4) + 0];  // R
                    dst[(i * 3) + 1] = byte_src[(i * 4) + 1];  // G
                    dst[(i * 3) + 2] = byte_src[(i * 4) + 2];  // B
                    // Skip byte_src[i*4 + 3] (Alpha)
                }
            };

            fast_abgr_to_rgb_le(frame_buffer_.data(), pixel, width * height);
        } else {
            std::memcpy(pixel, frame_buffer_.data(), required_size);
        }
    }
    // 2. Perform the copy (and theoretically, rotation/color conversion)

    // Update the out parameter with the actual number of bytes written
    *c_pixels = required_size;

    // 3. Return the exact FrameInfo associated with the pixels we just grabbed
    return Seq();
}

void VirtualDisplay::SendMultiTouchEvent(uint8_t slot, int x, int y, MultiTouchType type) {
    const absl::MutexLock lock(send_lock_);
    if (!vhid_) return;

    const Dimensions dims = GetDimensions();

    qemu_loop_
            ->Post([vhid = vhid_, slot, x, y, type, dims]() {
                InputHandler::SendMultiTouchEvent(vhid, slot, x, y, type, dims);
            })
            .IgnoreError();

    VLOG(2) << "[VirtualDisplay " << static_cast<int>(Id()) << "] MultiTouch -> "
            << "Slot: " << static_cast<int>(slot) << ", X: " << x << ", Y: " << y
            << ", Type: " << static_cast<int>(type) << "\n";
}

void VirtualDisplay::SendMouseEvent(int x, int y, int button_mask) {
    const absl::MutexLock lock(send_lock_);
    if (!vhid_) return;

    const Dimensions dims = GetDimensions();

    qemu_loop_
            ->Post([vhid = vhid_, x, y, button_mask, dims]() {
                InputHandler::SendMouseEvent(vhid, x, y, button_mask, dims);
            })
            .IgnoreError();

    // E. Update State
    last_bmask_ = button_mask;
    VLOG(2) << "[VirtualDisplay " << static_cast<int>(Id()) << "] Mouse -> "
            << "X: " << x << ", Y: " << y << ", Buttons: " << button_mask << "\n";
}

void VirtualDisplay::SendEvDevEvent(uint16_t type, uint16_t code, uint32_t value) {
    const absl::MutexLock lock(send_lock_);
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
