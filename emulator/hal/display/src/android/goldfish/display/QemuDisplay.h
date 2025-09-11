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

#include "android/goldfish/display/Display.h"
#include "android/goldfish/display/PixmanDisplay.h"

extern "C" {
#include "pixman.h"
#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/surface.h"

typedef struct VirtIOInputHID VirtIOInputHID;
}

namespace android::goldfish {

class QemuDisplay : public PixmanDisplay {
  public:
    QemuDisplay(EventLoop* loop, EventLoop* qemuLoop, QemuConsole* console, DisplaySurface* ds,
                int id);
    void sendMultiTouchEvent(uint8_t slot, int x, int y, MultiTouchType type) override;
    void sendMouseEvent(int x, int y, int button_mask) override;
    void sendEvDevEvent(uint16_t type, uint16_t code, uint32_t value) override;

  private:
    template <typename Sink>
    friend void AbslStringify(Sink&, const QemuDisplay&);
    QemuConsole* mConsole;
    EventLoop* mQemuLoop;
    ::VirtIOInputHID* mVhid;
    int mlast_bmask ABSL_GUARDED_BY(mSendLock) = 0;
    struct touch_slot mTouchSlots[INPUT_EVENT_SLOTS_MAX];
    absl::Mutex mSendLock;
};

template <typename Sink>
void AbslStringify(Sink& sink, const QemuDisplay& display) {
    absl::Format(&sink, "QemuDisplay: %s, con: %p", display.string(), display.mConsole);
}

}  // namespace android::goldfish
