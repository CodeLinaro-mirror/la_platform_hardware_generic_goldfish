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
#include "android/emulation/control/event_sender.h"

#include "absl/log/log.h"

#include "android/status/status_macros.h"

namespace android::emulation::control {

namespace {

template <class T>
absl::StatusOr<std::shared_ptr<IDisplay>> TryLockDisplay(IMultiDisplay& multidisplay,
                                                         const T& event) {
    auto screen = multidisplay.GetDisplay(event.display());
    if (!screen.ok()) {
        return absl::InvalidArgumentError(absl::StrFormat("Invalid display: %d", event.display()));
    }
    auto display = screen->lock();
    if (!display) {
        return absl::InvalidArgumentError(absl::StrFormat("Invalid display: %d", event.display()));
    }
    return display;
}

}  // namespace

absl::Status InputEventSender::Send(const AndroidEvent& event) const {
    ASSIGN_OR_RETURN(auto display, TryLockDisplay(*multi_display_, event));
    display->SendEvDevEvent(event.type(), event.code(), event.value());
    return absl::OkStatus();
};

absl::Status InputEventSender::Send(const MouseEvent& event) const {
    ASSIGN_OR_RETURN(auto display, TryLockDisplay(*multi_display_, event));

    display->SendMouseEvent(event.x(), event.y(), event.buttons());
    return absl::OkStatus();
}

absl::Status InputEventSender::Send(const WheelEvent& event) {
    LOG(ERROR) << "Wheel events are not yet supported, dropping event: "
               << event.ShortDebugString();
    return absl::OkStatus();
}

absl::Status InputEventSender::Send(const PenEvent& event) {
    ASSIGN_OR_RETURN(auto display, TryLockDisplay(*multi_display_, event));
    pointer_dispatcher_.SendEvents(*display, internal::PenTouchEvent::FromProto(event));
    return absl::OkStatus();
}

absl::Status InputEventSender::Send(const TouchEvent& event) {
    ASSIGN_OR_RETURN(auto display, TryLockDisplay(*multi_display_, event));
    pointer_dispatcher_.SendEvents(*display, internal::MultiTouchEvent::FromProto(event));
    return absl::OkStatus();
}

InputEventSender::InputEventSender(IMultiDisplay* multidisplay) : multi_display_(multidisplay) {}

}  // namespace android::emulation::control
