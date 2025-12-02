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
#include <vector>

#include "android/emulation/control/input/EvDevEvent.h"
#include "android/emulation/control/input/SlotRegistry.h"
#include "goldfish/display/Display.h"
#include "emulator_controller.grpc.pb.h"

namespace android {
namespace emulation {
namespace control {
namespace internal {

using EvDevEvents = std::vector<EvDevEvent>;

/**
 * @brief Internal representation of a protobuf Touch.
 *
 * This struct holds the data for a single touch point, including its
 * coordinates, identifier, pressure, and other touch-related properties.
 */
struct Touch {
    uint32_t x;            ///< The x-coordinate of the touch.
    uint32_t y;            ///< The y-coordinate of the touch.
    uint32_t identifier;   ///< A unique identifier for the touch.
    uint32_t pressure;     ///< The pressure applied by the touch.
    uint32_t touch_major;  ///< The major axis of the touch area.
    uint32_t touch_minor;  ///< The minor axis of the touch area.
    uint32_t orientation;  ///< The orientation of the touch.

    /**
     * @brief Converts the Touch data to a series of EvDevEvents.
     *
     * @param w The width of the display.
     * @param h The height of the display.
     * @param registry The SlotRegistry to use for managing touch slots.
     * @return A vector of EvDevEvents representing the touch.
     */
    virtual EvDevEvents toEvDevEvents(int w, int h, SlotRegistry* registry) const;
};

/**
 * @brief Internal representation of a protobuf Pen.
 *
 * This struct extends the Touch struct to include pen-specific properties
 * such as button press state and rubber pointer status.
 */
struct Pen : public Touch {
    bool button_pressed;  ///< True if a button on the pen is pressed.
    bool rubber_pointer;  ///< True if the pen is using the rubber pointer.

    /**
     * @brief Converts the Pen data to a series of EvDevEvents.
     *
     * @param w The width of the display.
     * @param h The height of the display.
     * @param registry The SlotRegistry to use for managing touch slots.
     * @return A vector of EvDevEvents representing the pen event.
     */
    EvDevEvents toEvDevEvents(int w, int h, SlotRegistry* registry) const override;
};

/**
 * @brief Internal representation of a protobuf PenEvent.
 *
 * This struct holds a collection of Pen touches.
 */
struct PenTouchEvent {
    std::vector<Pen> touches;  ///< A vector of Pen touches.

    /**
     * @brief Creates a PenTouchEvent from a protobuf PenEvent.
     *
     * @param proto The protobuf PenEvent to convert.
     * @return A PenTouchEvent representing the protobuf event.
     */
    static PenTouchEvent fromProto(const ::android::emulation::control::PenEvent& proto);
};

/**
 * @brief Internal representation of a protobuf TouchEvent.
 *
 * This struct holds a collection of Touch touches.
 */
struct MultiTouchEvent {
    std::vector<Touch> touches;  ///< A vector of Touch touches.

    /**
     * @brief Creates a MultiTouchEvent from a protobuf TouchEvent.
     *
     * @param proto The protobuf TouchEvent to convert.
     * @return A MultiTouchEvent representing the protobuf event.
     */
    static MultiTouchEvent fromProto(const ::android::emulation::control::TouchEvent& proto);
};

}  // namespace internal

using ::goldfish::display::IDisplay;

/**
 * @brief Dispatches pointer events (touch and pen) to the emulator.
 *
 * This class is responsible for receiving high-level pointer events
 * (MultiTouchEvent, PenTouchEvent) and converting them into low-level
 * EvDevEvents that can be sent to the Android kernel. It also manages
 * the SlotRegistry for multi-touch events.
 */
class PointerEventDispatcher {
  public:
    /**
     * @brief Sends a MultiTouchEvent to the emulator.
     *
     * This method iterates through the touch points in the MultiTouchEvent and
     * dispatches the corresponding evdev events to the specified display.
     * It also manages the internal state of used slots in the SlotRegistry.
     *
     * @param display The display to send the events to.
     * @param event The MultiTouchEvent to send.
     */
    void sendEvents(IDisplay& display, const internal::MultiTouchEvent& event);

    /**
     * @brief Sends a PenTouchEvent to the emulator.
     *
     * This method iterates through the touch points in the PenTouchEvent and
     * dispatches the corresponding evdev events to the specified display.
     * It also manages the internal state of used slots in the SlotRegistry.
     *
     * @param display The display to send the events to.
     * @param event The PenTouchEvent to send.
     */
    void sendEvents(IDisplay& display, const internal::PenTouchEvent& event);

  protected:                 /* This allows for testing. */
    SlotRegistry mRegistry;  ///< The SlotRegistry used to manage touch slots.
};

}  // namespace control
}  // namespace emulation
}  // namespace android
