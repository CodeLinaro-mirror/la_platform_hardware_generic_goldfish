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
#include "input_session.h"

#include <cmath>
#include <utility>

#include "absl/log/log.h"

#include "standard-headers/linux/input-event-codes.h"
#include "standard-headers/linux/input.h"
#include "touch_encoding.h"

namespace goldfish::grpc::v2 {

namespace {

using ::android::emulation::control::KeyboardEvent;
using ::android::emulation::v2::AxisType;
using ::android::emulation::v2::ButtonMask;
using ::android::emulation::v2::KeyAction;
using ::android::emulation::v2::PointerAction;

/**
 * @brief Safely converts a floating-point delta/scroll value to a Linux evdev __s32 integer value.
 *
 * In W3C DOM (MouseEvent.movementX/Y, WheelEvent.deltaX/Y) and Android MotionEvent, relative
 * movement and scroll deltas are defined as floating-point numbers to support sub-pixel precision
 * from high-DPI mice, precision touchpads, and smooth-scrolling gestures.
 *
 * In Linux evdev (struct input_event), the value field is a signed 32-bit integer (__s32), though
 * goldfish represents it as uint32_t. Converting a negative float directly to uint32_t is Undefined
 * Behavior in C++ ([conv.fpint]), so we truncate float -> int32_t first before casting to uint32_t.
 */
constexpr uint32_t ToEvdevValue(float value) {
    if (std::isnan(value)) {
        return 0;
    }
    return static_cast<uint32_t>(static_cast<int32_t>(value));
}

struct ButtonMapping {
    ButtonMask mask;
    uint16_t code;
};

constexpr ButtonMapping kButtonMappings[] = {
    {ButtonMask::BUTTON_MASK_PRIMARY, BTN_LEFT},
    {ButtonMask::BUTTON_MASK_SECONDARY, BTN_RIGHT},
    {ButtonMask::BUTTON_MASK_TERTIARY, BTN_MIDDLE},
    {ButtonMask::BUTTON_MASK_BACK, BTN_SIDE},
    {ButtonMask::BUTTON_MASK_FORWARD, BTN_EXTRA},
    {ButtonMask::BUTTON_MASK_STYLUS_PRIMARY, BTN_STYLUS},
    {ButtonMask::BUTTON_MASK_STYLUS_SECONDARY, BTN_STYLUS2},
};

/**
 * @brief Translates mouse button transitions into EV_KEY events.
 *
 * Compares current combined button bitmask with last_button_mask using XOR to identify transitions.
 */
void AppendButtonEvents(uint32_t current_buttons, uint32_t& last_button_mask, EvDevEvents& events) {
    const uint32_t diff_buttons = current_buttons ^ last_button_mask;
    if (diff_buttons == 0) {
        return;
    }

    for (const auto& [mask, code] : kButtonMappings) {
        if (diff_buttons & mask) {
            events.push_back({EV_KEY, code, (current_buttons & mask) ? 1U : 0U});
        }
    }
    last_button_mask = current_buttons;
}

void AppendButtonEvents(const PointerEvent& pointer, uint32_t& last_button_mask,
                        EvDevEvents& events) {
    int32_t current_buttons = 0;
    for (const auto& p : pointer.pointers()) {
        current_buttons |= p.button_mask();
    }
    AppendButtonEvents(static_cast<uint32_t>(current_buttons), last_button_mask, events);
}

/**
 * @brief Translates wheel scrolling values into EV_REL scroll events.
 *
 * Example:
 *   scroll_y = -3.0 (scroll wheel down)
 *   -> Emits {EV_REL, REL_WHEEL, -3}
 */
void AppendScrollEvents(const PointerEvent& pointer, EvDevEvents& events) {
    if (pointer.has_scroll_x() && pointer.scroll_x() != 0.0F) {
        events.push_back({EV_REL, REL_HWHEEL, ToEvdevValue(pointer.scroll_x())});
    }
    if (pointer.has_scroll_y() && pointer.scroll_y() != 0.0F) {
        events.push_back({EV_REL, REL_WHEEL, ToEvdevValue(pointer.scroll_y())});
    }
}

/**
 * @brief Translates relative mouse motion deltas into EV_REL displacement events.
 *
 * Handles Pointer Lock API deltas from web clients under standard POINTER_ACTION_MOVE or
 * POINTER_ACTION_RELATIVE_MOVE.
 *
 * Example:
 *   delta_x = 10.0, delta_y = -4.0
 *   -> Emits {EV_REL, REL_X, 10}, {EV_REL, REL_Y, -4}
 */
void AppendRelativeMotionEvents(const PointerEvent& pointer, EvDevEvents& events) {
    for (const auto& p : pointer.pointers()) {
        if (pointer.action() == PointerAction::POINTER_ACTION_RELATIVE_MOVE ||
            p.delta_x() != 0.0F || p.delta_y() != 0.0F) {
            if (p.delta_x() != 0.0F) {
                events.push_back({EV_REL, REL_X, ToEvdevValue(p.delta_x())});
            }
            if (p.delta_y() != 0.0F) {
                events.push_back({EV_REL, REL_Y, ToEvdevValue(p.delta_y())});
            }
        }
    }
}

/**
 * @brief Appends Multi-Touch Protocol B pointer release / lift events (kMtsPointerUp).
 */
void AppendPointerUp(int32_t pointer_id, SlotRegistry& slot_registry, EvDevEvents& events) {
    if (!slot_registry.IsIdentifierRegistered(pointer_id)) {
        return;
    }
    const int slot = slot_registry.AcquireSlot(pointer_id);
    if (slot >= 0) {
        AppendContactUp(slot, events);
    }
    slot_registry.ReleaseSlot(pointer_id);
}

/**
 * @brief Appends Multi-Touch Protocol B pointer down, move, or hover tracking events.
 */
void AppendPointerDownOrMove(const PointerEvent::Pointer& p, PointerAction action,
                             SlotRegistry& slot_registry, EvDevEvents& events) {
    if (action == PointerAction::POINTER_ACTION_DOWN) {
        slot_registry.ReleaseSlot(p.pointer_id());
    }
    const bool is_new_touch = !slot_registry.IsIdentifierRegistered(p.pointer_id());
    const int slot = slot_registry.AcquireSlot(p.pointer_id());
    if (slot < 0) {
        return;
    }
    AppendContactDownOrMove(p, action, slot, is_new_touch, events);
}

/**
 * @brief Translates absolute touch/pointer tracking into Linux Multi-Touch Protocol B slot events.
 *
 * Handles:
 * - Touch down & tracking ID allocation.
 * - Absolute coordinate normalization ([0.0, 1.0] -> [0, 32767]).
 * - Pressure, touch major size, orientation radians, and tool type mapping.
 * - Pointer lift / cancel tracking ID removal (kMtsPointerUp).
 * - Stylus / mouse hover tracking (pressure = 0 with POINTER_ACTION_HOVER_MOVE).
 *
 * Bypassed entirely when pointer.action() == POINTER_ACTION_RELATIVE_MOVE.
 */
void AppendTouchTrackingEvents(const PointerEvent& pointer, SlotRegistry& slot_registry,
                               EvDevEvents& events) {
    if (pointer.action() == PointerAction::POINTER_ACTION_RELATIVE_MOVE) {
        return;
    }

    for (const auto& p : pointer.pointers()) {
        if (IsContactReleased(pointer.action(), p)) {
            AppendPointerUp(p.pointer_id(), slot_registry, events);
        } else {
            AppendPointerDownOrMove(p, pointer.action(), slot_registry, events);
        }
    }
}

}  // namespace

InputSession::InputSession(IMultiDisplay& multi_display,
                           std::shared_ptr<IKeyEventSender> key_event_sender, bool hw_sensor_hinge)
        : multi_display_(&multi_display)
        , key_event_sender_(std::move(key_event_sender))
        , hw_sensor_hinge_(hw_sensor_hinge) {}

InputSession::~InputSession() {
    Cleanup();
}

void swap(InputSession& first, InputSession& second) noexcept {
    using std::swap;
    swap(first.multi_display_, second.multi_display_);
    swap(first.key_event_sender_, second.key_event_sender_);
    swap(first.hw_sensor_hinge_, second.hw_sensor_hinge_);
    swap(first.slot_registry_, second.slot_registry_);
    swap(first.last_button_mask_, second.last_button_mask_);
    swap(first.touched_displays_, second.touched_displays_);
}

InputSession::InputSession(InputSession&& other) noexcept {
    swap(*this, other);
}

InputSession& InputSession::operator=(InputSession&& other) noexcept {
    if (this != &other) {
        InputSession temp(std::move(other));
        swap(*this, temp);
    }
    return *this;
}

absl::Status InputSession::SendEventsToDisplay(uint32_t display_id, EvDevEvents events) {
    if (events.empty()) {
        return absl::OkStatus();
    }
    if (!multi_display_) {
        LOG(WARNING) << "InputSession::SendEventsToDisplay: multi_display_ is null!";
        return absl::FailedPreconditionError(
                "InputSession is not associated with a display manager.");
    }
    auto res = multi_display_->GetActiveDisplay(display_id, hw_sensor_hinge_);
    if (!res.ok()) {
        LOG(WARNING) << "InputSession::SendEventsToDisplay: GetActiveDisplay(" << display_id
                     << ") failed: " << res.status();
        return res.status();
    }
    VLOG(1) << "InputSession::SendEventsToDisplay: display_id=" << display_id << ", sending "
            << events.size() << " evdev events to display";
    auto display = *res;
    for (const auto& ev : events) {
        VLOG(2) << "  EvDevEvent: type=" << ev.type << ", code=" << ev.code
                << ", value=" << ev.value;
    }
    display->SendEvDevReport(std::move(events));
    return absl::OkStatus();
}

absl::Status InputSession::DispatchInputEvent(const InputEvent& event) {
    VLOG(1) << "InputSession::DispatchInputEvent: " << event.ShortDebugString();
    switch (event.event_case()) {
    case InputEvent::kPointer:
        return HandlePointerEvent(event.pointer());
    case InputEvent::kKey:
        return HandleKeyEvent(event.key());
    case InputEvent::kText:
        return HandleTextEvent(event.text());
    case InputEvent::kAxis:
        return HandleAxisEvent(event.axis());
    case InputEvent::EVENT_NOT_SET:
        return absl::OkStatus();
    }
    return absl::OkStatus();
}

absl::Status InputSession::HandlePointerEvent(const PointerEvent& pointer) {
    touched_displays_.insert(pointer.display_id());

    auto events = ToEvDevEvents(pointer, slot_registry_, last_button_mask_);
    auto expired = slot_registry_.ExpireOldSlots();
    if (!expired.empty()) {
        events.insert(events.end(), std::make_move_iterator(expired.begin()),
                      std::make_move_iterator(expired.end()));
    }
    VLOG(1) << "InputSession::HandlePointerEvent: display=" << pointer.display_id()
            << ", action=" << static_cast<int>(pointer.action())
            << ", pointers=" << pointer.pointers_size() << " -> generated " << events.size()
            << " evdev events";

    return SendEventsToDisplay(pointer.display_id(), std::move(events));
}

absl::Status InputSession::HandleKeyEvent(const KeyEvent& key) {
    if (!key_event_sender_) {
        return absl::OkStatus();
    }

    KeyboardEvent request;

    switch (key.action()) {
    case KeyAction::KEY_ACTION_DOWN:
        request.set_eventtype(KeyboardEvent::keydown);
        break;
    case KeyAction::KEY_ACTION_UP:
        request.set_eventtype(KeyboardEvent::keyup);
        break;
    case KeyAction::KEY_ACTION_PRESS:
    case KeyAction::KEY_ACTION_UNSPECIFIED:
    default:
        request.set_eventtype(KeyboardEvent::keypress);
        break;
    }

    switch (key.key_identifier_case()) {
    case KeyEvent::kDomCode:
        request.set_key(key.dom_code());
        break;
    case KeyEvent::kAndroidKeycode: {
        uint32_t evdev = AndroidKeycodeToEvdev(key.android_keycode());
        if (evdev > 0) {
            request.set_keycode(evdev);
            request.set_codetype(KeyboardEvent::Evdev);
        } else {
            VLOG(1) << "HandleKeyEvent: Unmapped Android keycode: " << key.android_keycode();
            return absl::InvalidArgumentError("Unmapped Android keycode");
        }
        break;
    }
    case KeyEvent::kEvdevCode:
        request.set_keycode(key.evdev_code());
        request.set_codetype(KeyboardEvent::Evdev);
        break;
    case KeyEvent::KEY_IDENTIFIER_NOT_SET:
        return absl::OkStatus();
    }

    key_event_sender_->send(std::move(request));
    return absl::OkStatus();
}

absl::Status InputSession::HandleTextEvent(const TextEvent& text) {
    if (key_event_sender_ && !text.text().empty()) {
        KeyboardEvent request;
        request.set_text(text.text());
        key_event_sender_->send(std::move(request));
    }
    return absl::OkStatus();
}

absl::Status InputSession::HandleAxisEvent(const AxisEvent& axis) {
    if (axis.axis() == AxisType::AXIS_TYPE_ROTARY_ENCODER) {
        const int32_t ticks = static_cast<int32_t>(std::round(axis.value()));
        if (ticks != 0) {
            return SendEventsToDisplay(0, {{EV_REL, REL_WHEEL, static_cast<uint32_t>(ticks)}});
        }
    }
    return absl::OkStatus();
}

void InputSession::Cleanup() {
    if (!multi_display_) {
        return;
    }

    auto released_slots = slot_registry_.ReleaseAllSlots();
    EvDevEvents release_events(released_slots.begin(), released_slots.end());
    AppendButtonEvents(0, last_button_mask_, release_events);

    if (!release_events.empty()) {
        for (int32_t display_id : touched_displays_) {
            (void)SendEventsToDisplay(display_id, release_events);
        }
    }
    touched_displays_.clear();
}

EvDevEvents InputSession::ToEvDevEvents(const PointerEvent& pointer, SlotRegistry& slot_registry,
                                        uint32_t& last_button_mask) {
    EvDevEvents events;
    events.reserve(pointer.pointers_size() * 8 + 8);

    AppendButtonEvents(pointer, last_button_mask, events);
    AppendScrollEvents(pointer, events);
    AppendRelativeMotionEvents(pointer, events);
    AppendTouchTrackingEvents(pointer, slot_registry, events);

    return events;
}

uint32_t InputSession::AndroidKeycodeToEvdev(int32_t keycode) {
    switch (keycode) {
    case 3:  // AKEYCODE_HOME
        return KEY_HOMEPAGE;
    case 4:  // AKEYCODE_BACK
        return KEY_BACK;
    case 24:  // AKEYCODE_VOLUME_UP
        return KEY_VOLUMEUP;
    case 25:  // AKEYCODE_VOLUME_DOWN
        return KEY_VOLUMEDOWN;
    case 26:  // AKEYCODE_POWER
        return KEY_POWER;
    case 27:  // AKEYCODE_CAMERA
        return KEY_CAMERA;
    case 82:  // AKEYCODE_MENU
        return KEY_MENU;
    case 84:  // AKEYCODE_SEARCH
        return KEY_SEARCH;
    case 187:  // AKEYCODE_APP_SWITCH
        return KEY_APPSELECT;
    default:
        return 0;
    }
}

}  // namespace goldfish::grpc::v2
