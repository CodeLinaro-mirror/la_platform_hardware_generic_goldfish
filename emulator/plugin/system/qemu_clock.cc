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
#include "android/base/qemu_clock.h"

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "qemu/timer.h"
// IWYU pragma: end_keep
// clang-format on
}

namespace android::base {

absl::Time QemuClock::now(ClockType type) const {
    QEMUClockType qemu_type;
    switch (type) {
    case ClockType::Virtual:
        qemu_type = QEMU_CLOCK_VIRTUAL;
        break;
    case ClockType::Host:
        qemu_type = QEMU_CLOCK_HOST;
        break;
    case ClockType::Realtime:
        qemu_type = QEMU_CLOCK_REALTIME;
        break;
    }
    int64_t ns = qemu_clock_get_ns(qemu_type);
    return absl::FromUnixNanos(ns);
}

}  // namespace android::base
