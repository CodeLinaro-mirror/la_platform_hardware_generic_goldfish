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

#include <cstdint>

#include "goldfish/display/display.h"

struct VirtIOInputHID;

namespace goldfish::display {

/**
 * @class InputHandler
 * @brief Utility class to handle coordinate translation, scaling, and
 *        sending input events to virtio-input devices.
 */
class InputHandler {
  public:
    /**
     * @brief Translates and scales coordinates, then sends a multi-touch event.
     *
     * @param vhid The virtio-input device handle.
     * @param slot The touch slot.
     * @param x The logical X coordinate.
     * @param y The logical Y coordinate.
     * @param type The multi-touch event type.
     * @param dims The current physical dimensions of the display.
     */
    static void SendMultiTouchEvent(VirtIOInputHID* vhid, uint8_t slot, int x, int y,
                                    MultiTouchType type, const Dimensions& dims);

    /**
     * @brief Translates and scales coordinates, then sends a mouse event
     *        mapped to a multi-touch event.
     *
     * @param vhid The virtio-input device handle.
     * @param x The logical X coordinate.
     * @param y The logical Y coordinate.
     * @param button_mask The mouse button mask.
     * @param dims The current physical dimensions of the display.
     */
    static void SendMouseEvent(VirtIOInputHID* vhid, int x, int y, int button_mask,
                               const Dimensions& dims);
};

}  // namespace goldfish::display
