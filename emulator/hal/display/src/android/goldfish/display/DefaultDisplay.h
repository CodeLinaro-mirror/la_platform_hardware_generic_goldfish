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

#include "android/goldfish/display/Display.h"  // Include the IDisplay definition

extern "C" {
#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/surface.h"

typedef struct VirtIOInputHID VirtIOInputHID;
}

namespace android::goldfish {

class DefaultDisplay : public IDisplay {
  public:
    DefaultDisplay(QemuConsole* console, DisplaySurface* ds, int id);
    void replaceSurface(DisplaySurface* newSurface);
    absl::StatusOr<FrameInfo> getPixels(PixelFormat format, int newWidth, int newHeight,
                                        int rotation, uint8_t* pixels,
                                        size_t* cPixels) const override;
    void sendMultiTouchEvent(uint8_t slot, int x, int y, MultiTouchType type) override;
    void sendMouseEvent(int x, int y, int button_mask) override;
    void updateSurface(int x, int y, int width, int height);
    void sendEvDevEvent(uint16_t type, uint16_t code, uint32_t value) override;

  private:
    mutable absl::Mutex mDisplayAccess;
    DisplaySurface* mDisplaySurface;
    QemuConsole* mConsole;

    ::VirtIOInputHID* mVhid;
    int mlast_bmask{0};
    struct touch_slot mTouchSlots[INPUT_EVENT_SLOTS_MAX];
};

}  // namespace android::goldfish
