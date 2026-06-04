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

#pragma once

#include <memory>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"

#include "goldfish/async/event_loop.h"
#include "goldfish/display/display.h"
#include "goldfish/fps_calculator.h"

extern "C" {
#include "pixman.h"
#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/surface.h"

using VirtIOInputHID = struct VirtIOInputHID;
}

namespace goldfish::display {

class VirtualDisplay : public IDisplay {
  public:
    // Constructor passes essential initialization data up to the IDisplay base class
    VirtualDisplay(EventLoop* loop, EventLoop* qloop, uint8_t id, uint32_t width, uint32_t height,
                   uint32_t dpi, uint32_t flags);

    ~VirtualDisplay() override;

    // --- Overrides for pure virtual methods from IDisplay ---

    std::pair<int, int> ResizeKeepAspectRatio(int desired_width, int desired_height) override;
    absl::StatusOr<FrameInfo> GetPixels(PixelFormat fmt, int width, int height,
                                        ImageRotation rotation, uint8_t* pixel,
                                        size_t* c_pixels) const override;

    void SendMultiTouchEvent(uint8_t slot, int x, int y, MultiTouchType type) override;

    void SendMouseEvent(int x, int y, int button_mask) override;

    void SendEvDevEvent(uint16_t type, uint16_t code, uint32_t value) override;

    // --- Concrete class specific methods ---

    // A helper method to simulate the guest OS finishing a rendered frame
    void SimulateGuestFrameUpdate();

    uint32_t GetDpi() const { return dpi_; }
    uint32_t GetFlags() const { return flags_; }

  private:
    std::vector<uint8_t> frame_buffer_;
    std::shared_ptr<EventLoop::Timer> one_second_timer_;
    void OneFrameTick();

    EventLoop* qemu_loop_;
    ::VirtIOInputHID* vhid_;
    uint32_t dpi_;
    uint32_t flags_;
    int last_bmask_ ABSL_GUARDED_BY(send_lock_) = 0;
    absl::Mutex send_lock_;
};

}  // namespace goldfish::display
