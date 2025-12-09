// Copyright (C) 2025 The Android Open Source Project
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

#include "absl/status/status.h"

#include "android/emulation/control/ev_dev_event.h"
#include "android/emulation/control/internal/pointer_event_dispatcher.h"
#include "emulator_controller.grpc.pb.h"
#include "goldfish/display/QemuMultidisplay/multi_display.h"

namespace android::emulation::control {

using ::goldfish::display::IDisplay;
using ::goldfish::display::IMultiDisplay;

/**
 * @brief Sends input events to the emulator's displays.
 *
 * This class is responsible for receiving high-level input events (mouse,
 * evdev, touch, pen, wheel) and dispatching them to the appropriate
 * display within the emulator. It handles display locking and error
 * checking to ensure that events are sent correctly.
 */
class InputEventSender {
  public:
    /**
     * @brief Constructs an InputEventSender.
     *
     * @param multidisplay The IMultiDisplay instance used to manage multiple displays.
     */
    InputEventSender(IMultiDisplay* multidisplay);

    /**
     * @brief Sends a mouse event to the emulator.
     *
     * @param event The MouseEvent to send.
     * @return An absl::Status indicating success or failure.
     */
    absl::Status send(const MouseEvent& event) const;

    /**
     * @brief Sends a generic Android event (a raw evdev event) to the emulator.
     *
     * @param event The AndroidEvent to send.
     * @return An absl::Status indicating success or failure.
     */
    absl::Status send(const AndroidEvent& event) const;

    /**
     * @brief Sends a wheel event to the emulator.
     *
     * @param event The WheelEvent to send.
     * @return An absl::Status indicating success or failure.
     * @note Wheel events are currently not supported.
     */
    absl::Status send(const WheelEvent& event) const;

    /**
     * @brief Sends a touch event to the emulator.
     *
     * @param touch The TouchEvent to send.
     * @return An absl::Status indicating success or failure.
     * @note This method will update the internal state of used slots in the SlotRegistry.
     */
    absl::Status send(const TouchEvent& touch);

    /**
     * @brief Sends a pen event to the emulator.
     *
     * @param event The PenEvent to send.
     * @return An absl::Status indicating success or failure.
     * @note This method will update the internal state of used slots in the SlotRegistry.
     */
    absl::Status send(const PenEvent& event);

  private:
    PointerEventDispatcher mPointerDispatcher;  ///< The dispatcher for pointer events (touch, pen).
    IMultiDisplay* mMultiDisplay;               ///< The multi-display manager.
};

}  // namespace android::emulation::control
