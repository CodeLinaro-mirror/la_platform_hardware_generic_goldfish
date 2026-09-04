// Copyright (C) 2026 The Android Open Source Project
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

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/types/span.h"

#include "android/emulation/control/ev_dev_event.h"
#include "android/emulation/control/keyboard/key_event_sender.h"
#include "goldfish/display/QemuMultidisplay/multi_display.h"
#include "input/input_service.pb.h"
#include "slot_registry.h"

namespace goldfish::grpc::v2 {

using ::android::emulation::control::EvDevEvent;
using ::android::emulation::control::SlotRegistry;
using ::android::emulation::control::keyboard::IKeyEventSender;
using ::android::emulation::v2::input::AxisEvent;
using ::android::emulation::v2::input::InputEvent;
using ::android::emulation::v2::input::KeyEvent;
using ::android::emulation::v2::input::PointerEvent;
using ::android::emulation::v2::input::TextEvent;
using ::goldfish::display::IMultiDisplay;

/**
 * @class InputSession
 * @brief Manages a single multi-modal input session (e.g. gRPC client stream or WebRTC
 * DataChannel).
 *
 * ### Architectural Role
 * Acts as the translation engine and stateful coordinator bridging heterogeneous high-level
 * input modalities (multi-touch pointers, mouse buttons/scrolling, keyboard scancodes, IME text,
 * and rotary encoders) into raw Linux evdev input events targeting virtual displays.
 *
 * ### Thread Safety
 * This class is **not thread-safe**. All method invocations (`DispatchInputEvent`, `Cleanup`, etc.)
 * must occur on a single thread (such as the event loop or gRPC stream processing thread).
 *
 * ### Ownership & Lifetimes
 * Holds non-owning raw pointer to `IMultiDisplay` (which must outlive this session) and an
 * optional `std::shared_ptr<IKeyEventSender>` for dispatching keyboard and text events.
 * Automatically flushes release events (`kMtsPointerUp` and key/button releases) upon destruction
 * or explicit `Cleanup()` invocation to prevent dangling touches or stuck keys in the guest OS.
 *
 * ### Android Desktop & Mouse Mode Compatibility
 * - Relative pointer deltas (`POINTER_ACTION_RELATIVE_MOVE`) are dispatched as Linux evdev `REL_X`,
 * `REL_Y`, `BTN_*`, and `REL_WHEEL`/`REL_HWHEEL`. Android's `InputReader` classifies this stream as
 * `SOURCE_MOUSE` (`INPUT_DEVICE_CLASS_CURSOR`), enabling desktop mouse cursor hover, window
 * dragging/resizing, right-click context menus, and wheel scrolling.
 * - Absolute coordinates targeting touch displays map `TOOL_TYPE_MOUSE` to `MT_TOOL_FINGER` to
 * conform to Linux multi-touch protocol capabilities on touchscreen surfaces.
 */
class InputSession {
  public:
    /**
     * @brief Constructs a new InputSession.
     *
     * @param multi_display MultiDisplay manager to route evdev events to specific displays.
     * @param key_event_sender Keyboard event sender interface for dispatching key and text events.
     * @param hw_sensor_hinge Whether the hardware foldable hinge sensor is enabled.
     */
    InputSession(IMultiDisplay& multi_display,
                 std::shared_ptr<IKeyEventSender> key_event_sender = nullptr,
                 bool hw_sensor_hinge = false);
    ~InputSession();

    InputSession(const InputSession&) = delete;
    InputSession& operator=(const InputSession&) = delete;
    InputSession(InputSession&& other) noexcept;
    InputSession& operator=(InputSession&& other) noexcept;

    friend void swap(InputSession& first, InputSession& second) noexcept;

    /**
     * @brief Dispatches an incoming heterogeneous v2 InputEvent based on its modality.
     *
     * @param event The incoming input event proto.
     * @return absl::Status Status of the dispatch operation.
     */
    absl::Status DispatchInputEvent(const InputEvent& event);

    /**
     * @brief Handles multi-touch pointers, mouse buttons, relative movement, and scrolling.
     *
     * @param pointer The incoming pointer event proto.
     * @return absl::Status Status of the pointer routing operation.
     */
    absl::Status HandlePointerEvent(const PointerEvent& pointer);

    /**
     * @brief Handles keyboard events (DOM codes, Android keycodes, and raw evdev scancodes).
     *
     * @param key The incoming key event proto.
     * @return absl::Status Status of the key dispatch operation.
     */
    absl::Status HandleKeyEvent(const KeyEvent& key);

    /**
     * @brief Handles direct UTF-8 string typing into guest input method editor (IME).
     *
     * @param text The incoming text event proto.
     * @return absl::Status Status of the text injection operation.
     */
    absl::Status HandleTextEvent(const TextEvent& text);

    /**
     * @brief Handles hardware axis events such as rotary crowns and automotive dials.
     *
     * @param axis The incoming axis event proto.
     * @return absl::Status Status of the axis dispatch operation.
     */
    absl::Status HandleAxisEvent(const AxisEvent& axis);

    /**
     * @brief Flushes release events for all active touch slots and mouse buttons across touched
     * displays.
     */
    void Cleanup();

    /**
     * @brief Translates a high-level PointerEvent proto into a stream of raw Linux evdev events.
     *
     * Handles:
     * - Mouse button transitions (`BTN_LEFT`, `BTN_RIGHT`, `BTN_MIDDLE`, `BTN_SIDE`, `BTN_EXTRA`,
     * `BTN_STYLUS`).
     * - Smooth wheel scrolling (`REL_WHEEL`, `REL_HWHEEL`).
     * - Relative pointer movement (`REL_X`, `REL_Y`) for mouse capture / Pointer Lock.
     * - Multi-touch Protocol B absolute slot tracking (`ABS_MT_SLOT`, `ABS_MT_TRACKING_ID`,
     *   `ABS_MT_POSITION_X`/`Y`, `ABS_MT_PRESSURE`, `ABS_MT_TOUCH_MAJOR`, `ABS_MT_ORIENTATION`,
     *   `ABS_TILT_X`, `ABS_TILT_Y`).
     *
     * @param pointer The incoming PointerEvent proto.
     * @param slot_registry The session's SlotRegistry managing MT tracking IDs and slots.
     * @param last_button_mask In/out parameter tracking previously pressed button bitmask.
     * @return std::vector<EvDevEvent> Sequence of raw evdev events (excluding trailing EV_SYN).
     */
    static std::vector<EvDevEvent> ToEvDevEvents(const PointerEvent& pointer,
                                                 SlotRegistry& slot_registry,
                                                 uint32_t& last_button_mask);

  private:
    absl::Status SendEventsToDisplay(uint32_t display_id, absl::Span<const EvDevEvent> events);

    IMultiDisplay* multi_display_{nullptr};  ///< Multi-display manager for evdev routing.
    std::shared_ptr<IKeyEventSender>
            key_event_sender_;     ///< Keyboard and IME text event dispatcher.
    bool hw_sensor_hinge_{false};  ///< Whether hardware hinge sensor folding is enabled.

    SlotRegistry slot_registry_;    ///< Active multi-touch slots and tracking ID mappings.
    uint32_t last_button_mask_{0};  ///< Bitmask of currently held mouse/pointer buttons.
    absl::flat_hash_set<uint32_t>
            touched_displays_;  ///< Display IDs touched in this session for cleanup.
};

}  // namespace goldfish::grpc::v2
