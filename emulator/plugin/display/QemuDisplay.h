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
#pragma once

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/display/pixman_display.h"

extern "C" {
#include "pixman.h"
#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/surface.h"

using VirtIOInputHID = struct VirtIOInputHID;
}

namespace goldfish::display {

class QemuDisplay : public PixmanDisplay {
  public:
    QemuDisplay(EventLoop* loop, EventLoop* qemu_loop, QemuConsole* con, DisplaySurface* ds,
                int index);
    ~QemuDisplay() override;
    void SendMultiTouchEvent(uint8_t slot, int x, int y, MultiTouchType type) override;
    void SendMouseEvent(int x, int y, int button_mask) override;
    void SendEvDevEvent(uint16_t type, uint16_t code, uint32_t value) override;

    void SetOwnedSurface(DisplaySurface* surface);

    QemuConsole* GetConsole() const override { return console_; }

  private:
    template <typename Sink>
    friend void AbslStringify(Sink&, const QemuDisplay&);
    QemuConsole* console_;
    EventLoop* qemu_loop_;
    ::VirtIOInputHID* vhid_;
    int last_bmask_ ABSL_GUARDED_BY(send_lock_) = 0;
    struct touch_slot touch_slots_[INPUT_EVENT_SLOTS_MAX];
    absl::Mutex send_lock_;
    DisplaySurface* owned_surface_{nullptr};
};

template <typename Sink>
void AbslStringify(Sink& sink, const QemuDisplay& display) {
    absl::Format(&sink, "QemuDisplay: %s, con: %p", display.String(), display.console_);
}

}  // namespace goldfish::display
