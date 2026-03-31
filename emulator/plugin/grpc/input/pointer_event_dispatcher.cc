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
#include "android/emulation/control/internal/pointer_event_dispatcher.h"

#include <vector>

#include "absl/log/log.h"

#include "standard-headers/linux/input-event-codes.h"
#include "standard-headers/linux/input.h"

constexpr uint32_t kEvAbsMin = 0x0000;
constexpr uint32_t kEvAbsMax = 0x7FFF;

namespace android::emulation::control {
namespace internal {
namespace {

uint32_t ScaleAxis(uint32_t value, uint32_t min_in, uint32_t max_in) {
    constexpr uint32_t kMinOut = kEvAbsMin;
    constexpr uint32_t kMaxOut = kEvAbsMax;
    constexpr uint32_t kRangeOut = kMaxOut - kMinOut;

    const uint32_t range_in = max_in - min_in;
    if (range_in < 1) {
        return kMinOut + (kRangeOut / 2);
    }
    return ((value - min_in) * kRangeOut / range_in) + kMinOut;
}
}  // namespace

MultiTouchEvent MultiTouchEvent::FromProto(const ::android::emulation::control::TouchEvent& proto) {
    MultiTouchEvent event;
    for (const auto& proto_touch : proto.touches()) {
        Touch touch;
        touch.x = proto_touch.x();
        touch.y = proto_touch.y();
        touch.identifier = proto_touch.identifier();
        touch.pressure = proto_touch.pressure();
        touch.touch_major = proto_touch.touch_major();
        touch.touch_minor = proto_touch.touch_minor();
        touch.orientation = proto_touch.orientation();
        event.touches.push_back(touch);
    }
    return event;
}

// C++ struct representation of PenEvent
PenTouchEvent PenTouchEvent::FromProto(const ::android::emulation::control::PenEvent& proto) {
    constexpr uint32_t kPenId = 0x0a;
    PenTouchEvent event;
    for (const auto& proto_pen : proto.events()) {
        Pen pen;
        pen.button_pressed = proto_pen.button_pressed();
        pen.rubber_pointer = proto_pen.rubber_pointer();
        pen.x = proto_pen.location().x();
        pen.y = proto_pen.location().y();
        pen.identifier = kPenId;
        pen.pressure = proto_pen.location().pressure();
        pen.touch_major = proto_pen.location().touch_major();
        pen.touch_minor = proto_pen.location().touch_minor();
        pen.orientation = proto_pen.location().orientation();
        event.touches.push_back(pen);
    }
    return event;
}

EvDevEvents Pen::ToEvDevEvents(uint32_t w, uint32_t h, SlotRegistry* registry) const {
    /* Special tracking ID for the pen pointer, the next one after the fingers.*/
    EvDevEvents events;
    const uint32_t ev_dev_pressed = this->button_pressed ? 1 : 0;
    if (this->rubber_pointer) {
        events.push_back({EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_MAX});
        events.push_back({EV_KEY, BTN_TOOL_RUBBER, ev_dev_pressed});
    } else {
        events.push_back({EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_PEN});
    }

    events.push_back({EV_KEY, BTN_STYLUS, ev_dev_pressed});

    auto touches = Touch::ToEvDevEvents(w, h, registry);
    events.insert(events.end(), touches.begin(), touches.end());
    return events;
}

EvDevEvents Touch::ToEvDevEvents(uint32_t w, uint32_t h, SlotRegistry* registry) const {
    EvDevEvents events;

    const bool is_registered = registry->IsIdentifierRegistered(this->identifier);
    if (this->pressure == 0 && !is_registered) {
        VLOG(1) << "Attempting to release touch event with identifier " << this->identifier
                << ", but it is not registered. This likely indicates an attempt to release a "
                   "touch that was never pressed or has already been released.";
        return events;
    }

    const int slot = registry->AcquireSlot(this->identifier);
    if (slot < 0) {
        return events;
    }

    const auto u_slot = static_cast<uint32_t>(slot);

    // Only register slot in guest if it as not yet registered.
    if (!is_registered) {
        events.push_back({EV_ABS, ABS_MT_TRACKING_ID, u_slot});
    }

    events.push_back({EV_ABS, ABS_MT_SLOT, u_slot});

    // Scale to proper evdev values.
    const uint32_t dx = ScaleAxis(this->x, 0, w);
    const uint32_t dy = ScaleAxis(this->y, 0, h);

    if (this->touch_major > 0) {
        events.push_back({EV_ABS, ABS_MT_TOUCH_MAJOR, this->touch_major});
    }

    if (this->touch_minor > 0) {
        events.push_back({EV_ABS, ABS_MT_TOUCH_MINOR, this->touch_minor});
    }

    if (this->orientation > 0) {
        events.push_back({EV_ABS, ABS_MT_ORIENTATION, this->orientation});
    }

    events.push_back({EV_ABS, ABS_MT_PRESSURE, this->pressure});
    events.push_back({EV_ABS, ABS_MT_POSITION_X, dx});
    events.push_back({EV_ABS, ABS_MT_POSITION_Y, dy});

    // Clean up slot if pressure is 0 (implies finger has been lifted)
    if (this->pressure == 0) {
        events.push_back({EV_ABS, ABS_MT_TRACKING_ID, kMtsPointerUp});
        events.push_back({EV_ABS, ABS_MT_ORIENTATION, 0});
        registry->ReleaseSlot(this->identifier);
    }

    return events;
}

}  // namespace internal

void PointerEventDispatcher::SendEvents(IDisplay& display, const internal::MultiTouchEvent& event) {
    auto dims = display.GetDimensions();
    const uint32_t w = dims.width;
    const uint32_t h = dims.height;

    for (const auto& touch : event.touches) {
        for (auto& evdev : touch.ToEvDevEvents(w, h, &registry_)) {
            display.SendEvDevEvent(evdev.type, evdev.code, evdev.value);
        }
    }

    for (auto& ev : registry_.ExpireOldSlots()) {
        display.SendEvDevEvent(ev.type, ev.code, ev.value);
    }

    display.SendEvDevEvent(EV_SYN, SYN_REPORT, 0);
}

void PointerEventDispatcher::SendEvents(IDisplay& display, const internal::PenTouchEvent& event) {
    auto dims = display.GetDimensions();
    const uint32_t w = dims.width;
    const uint32_t h = dims.height;

    for (const auto& touch : event.touches) {
        for (auto& evdev : touch.ToEvDevEvents(w, h, &registry_)) {
            display.SendEvDevEvent(evdev.type, evdev.code, evdev.value);
        }
    }

    for (auto& ev : registry_.ExpireOldSlots()) {
        display.SendEvDevEvent(ev.type, ev.code, ev.value);
    }

    display.SendEvDevEvent(EV_SYN, SYN_REPORT, 0);
}

}  // namespace android::emulation::control
