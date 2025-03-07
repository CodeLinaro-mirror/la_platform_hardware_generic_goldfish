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
#include "android/emulation/control/input/EventSender.h"

#include "absl/log/log.h"

#include "aemu/base/utils/status_macros.h"

namespace android {
namespace emulation {
namespace control {

template <class T>
absl::StatusOr<std::shared_ptr<IDisplay>> tryLockDisplay(IMultiDisplay& multidisplay,
                                                         const T& event) {
    auto screen = multidisplay.getDisplay(event.display());
    if (!screen.ok()) {
        return absl::InvalidArgumentError(absl::StrFormat("Invalid display: %d", event.display()));
    }
    auto display = screen->lock();
    if (!display) {
        return absl::InvalidArgumentError(absl::StrFormat("Invalid display: %d", event.display()));
    }
    return display;
}

absl::Status InputEventSender::send(const AndroidEvent& event) const {
    ASSIGN_OR_RETURN(auto display, tryLockDisplay(*mMultiDisplay, event));
    display->sendEvDevEvent(event.type(), event.code(), event.value());
    return absl::OkStatus();
};

absl::Status InputEventSender::send(const MouseEvent& event) const {
    ASSIGN_OR_RETURN(auto display, tryLockDisplay(*mMultiDisplay, event));

    display->sendMouseEvent(event.x(), event.y(), event.buttons());
    return absl::OkStatus();
}

absl::Status InputEventSender::send(const WheelEvent& event) const {
    LOG(ERROR) << "Wheel events are not yet supported, dropping event: "
               << event.ShortDebugString();
    return absl::UnimplementedError(
            "We do not yet support translation of wheel events to touch events.");
}

absl::Status InputEventSender::send(const PenEvent& event) {
    ASSIGN_OR_RETURN(auto display, tryLockDisplay(*mMultiDisplay, event));
    mPointerDispatcher.sendEvents(*display, internal::PenTouchEvent::fromProto(event));
    return absl::OkStatus();
}

absl::Status InputEventSender::send(const TouchEvent& event) {
    ASSIGN_OR_RETURN(auto display, tryLockDisplay(*mMultiDisplay, event));
    mPointerDispatcher.sendEvents(*display, internal::MultiTouchEvent::fromProto(event));
    return absl::OkStatus();
}
}  // namespace control
}  // namespace emulation
}  // namespace android
