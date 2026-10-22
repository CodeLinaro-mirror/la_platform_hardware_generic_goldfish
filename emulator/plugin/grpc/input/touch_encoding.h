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

#include "android/emulation/control/ev_dev_event.h"
#include "input_service.pb.h"

namespace goldfish::grpc::v2 {

// Encodes v2 touch contacts as Linux multi-touch protocol B evdev events.
//
// These functions are pure: they hold no state and never allocate slots. The
// caller owns slot allocation and passes in the slot a contact occupies. The
// slot number doubles as the contact's ABS_MT_TRACKING_ID. Events are
// appended to `events`; the caller terminates the report (SYN_REPORT).

/**
 * @brief Appends the events for a contact going down, moving, or hovering.
 *
 * Emits ABS_MT_SLOT `slot`, then ABS_MT_TRACKING_ID `slot` if `new_contact`,
 * then position, tool type, pressure (scaled to kMtsPressureMax; 0 while
 * hovering), touch major/minor, orientation (if non-zero) and, for a tilted
 * stylus or eraser, ABS_TILT_X/Y.
 *
 * @param pointer The contact's position and properties.
 * @param action The pointer action; hover actions encode zero pressure.
 * @param slot The multi-touch slot the contact occupies; must be >= 0.
 * @param new_contact Whether the contact just acquired `slot` and needs a
 *     tracking ID.
 * @param events The event list to append to.
 */
void AppendContactDownOrMove(const ::android::emulation::v2::PointerEvent::Pointer& pointer,
                             ::android::emulation::v2::PointerAction action, int slot,
                             bool new_contact, ::android::emulation::control::EvDevEvents& events);

/**
 * @brief Appends the events lifting the contact in `slot`.
 *
 * Emits ABS_MT_SLOT `slot`, then ABS_MT_TRACKING_ID kMtsPointerUp (-1).
 *
 * @param slot The multi-touch slot of the contact to lift; must be >= 0.
 * @param events The event list to append to.
 */
void AppendContactUp(int slot, ::android::emulation::control::EvDevEvents& events);

/**
 * @brief Determines if a pointer action or pressure indicator signifies a touch release /
 * cancel.
 *
 * - On CANCEL or HOVER_EXIT, all pointers in the event are released.
 * - On POINTER_ACTION_UP, pointers lift unless they explicitly report positive contact pressure
 *   (`p.has_pressure() && p.pressure() > 0.0F`), supporting multi-touch partial finger lifts.
 * - On POINTER_ACTION_DOWN, HOVER_ENTER, and HOVER_MOVE, contacts remain active.
 * - On POINTER_ACTION_MOVE, contacts remain active when `pressure` is omitted
 *   (`!p.has_pressure()`, which defaults to 1.0) and only lift if `pressure` is explicitly set
 *   to `<= 0.0F`.
 */
bool IsContactReleased(::android::emulation::v2::PointerAction action,
                       const ::android::emulation::v2::PointerEvent::Pointer& p);

}  // namespace goldfish::grpc::v2
