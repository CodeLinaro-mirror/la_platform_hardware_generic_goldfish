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

#include "absl/log/log.h"

#include "standard-headers/linux/input-event-codes.h"
#include "standard-headers/linux/input.h"

constexpr uint32_t EV_ABS_MIN = 0x0000;
constexpr uint32_t EV_ABS_MAX = 0x7FFF;

namespace android {
namespace emulation {
namespace control {
namespace internal {

static uint32_t scaleAxis(int value, int min_in, int max_in) {
    constexpr uint32_t min_out = EV_ABS_MIN;
    constexpr uint32_t max_out = EV_ABS_MAX;
    constexpr uint32_t range_out = (uint32_t)max_out - min_out;

    uint32_t range_in = (uint32_t)max_in - min_in;
    if (range_in < 1) {
        return min_out + range_out / 2;
    }
    return ((uint32_t)value - min_in) * range_out / range_in + min_out;
}

MultiTouchEvent MultiTouchEvent::fromProto(const ::android::emulation::control::TouchEvent& proto) {
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
PenTouchEvent PenTouchEvent::fromProto(const ::android::emulation::control::PenEvent& proto) {
    constexpr uint32_t penid = 0x0a;
    PenTouchEvent event;
    for (const auto& proto_pen : proto.events()) {
        Pen pen;
        pen.button_pressed = proto_pen.button_pressed();
        pen.rubber_pointer = proto_pen.rubber_pointer();
        pen.x = proto_pen.location().x();
        pen.y = proto_pen.location().y();
        pen.identifier = penid;
        pen.pressure = proto_pen.location().pressure();
        pen.touch_major = proto_pen.location().touch_major();
        pen.touch_minor = proto_pen.location().touch_minor();
        pen.orientation = proto_pen.location().orientation();
        event.touches.push_back(pen);
    }
    return event;
}

EvDevEvents Pen::toEvDevEvents(int w, int h, SlotRegistry* registry) const {
    /* Special tracking ID for the pen pointer, the next one after the fingers.*/
    EvDevEvents events;
    uint32_t ev_dev_pressed = this->button_pressed ? 1 : 0;
    if (this->rubber_pointer) {
        events.push_back({EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_MAX});
        events.push_back({EV_KEY, BTN_TOOL_RUBBER, ev_dev_pressed});
    } else {
        events.push_back({EV_ABS, ABS_MT_TOOL_TYPE, MT_TOOL_PEN});
    }

    events.push_back({EV_KEY, BTN_STYLUS, ev_dev_pressed});

    auto touches = Touch::toEvDevEvents(w, h, registry);
    events.insert(events.end(), touches.begin(), touches.end());
    return events;
}

EvDevEvents Touch::toEvDevEvents(int w, int h, SlotRegistry* registry) const {
    EvDevEvents events;

    bool isRegistered = registry->isIdentifierRegistered(this->identifier);
    if (this->pressure == 0 && !isRegistered) {
        VLOG(1) << "Attempting to release touch event with identifier " << this->identifier
                << ", but it is not registered. This likely indicates an attempt to release a "
                   "touch that was never pressed or has already been released.";
        return events;
    }

    uint32_t slot = registry->acquireSlot(this->identifier);
    if (slot < 0) {
        return events;
    }

    // Only register slot in guest if it as not yet registered.
    if (!isRegistered) {
        events.push_back({EV_ABS, ABS_MT_TRACKING_ID, slot});
    }

    events.push_back({EV_ABS, ABS_MT_SLOT, slot});

    // Scale to proper evdev values.
    uint32_t dx = scaleAxis(this->x, 0, w);
    uint32_t dy = scaleAxis(this->y, 0, h);

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
        events.push_back({EV_ABS, ABS_MT_TRACKING_ID, MTS_POINTER_UP});
        events.push_back({EV_ABS, ABS_MT_ORIENTATION, 0});
        registry->releaseSlot(this->identifier);
    }

    return events;
}

}  // namespace internal

void PointerEventDispatcher::sendEvents(IDisplay& display, const internal::MultiTouchEvent& event) {
    auto dims = display.GetDimensions();
    int w = dims.width;
    int h = dims.height;

    for (const auto& touch : event.touches) {
        for (auto& evdev : touch.toEvDevEvents(w, h, &mRegistry)) {
            display.SendEvDevEvent(evdev.type, evdev.code, evdev.value);
        }
    }

    for (auto& ev : mRegistry.expireOldSlots()) {
        display.SendEvDevEvent(ev.type, ev.code, ev.value);
    }

    display.SendEvDevEvent(EV_SYN, SYN_REPORT, 0);
}

void PointerEventDispatcher::sendEvents(IDisplay& display, const internal::PenTouchEvent& event) {
    auto dims = display.GetDimensions();
    int w = dims.width;
    int h = dims.height;

    for (const auto& touch : event.touches) {
        for (auto& evdev : touch.toEvDevEvents(w, h, &mRegistry)) {
            display.SendEvDevEvent(evdev.type, evdev.code, evdev.value);
        }
    }

    for (auto& ev : mRegistry.expireOldSlots()) {
        display.SendEvDevEvent(ev.type, ev.code, ev.value);
    }

    display.SendEvDevEvent(EV_SYN, SYN_REPORT, 0);
}

}  // namespace control
}  // namespace emulation
}  // namespace android
