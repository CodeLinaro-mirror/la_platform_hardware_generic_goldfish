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

#include <cstdint>

#include "absl/status/statusor.h"
#include "absl/types/span.h"

#include "android/emulation/control/ev_dev_event.h"
#include "contact_table.h"

namespace goldfish::grpc::v2 {

/**
 * @brief The guest touch input device of one physical display.
 *
 * ### Thread Safety:
 * Called only on the QEMU event loop.
 */
class TouchDeviceSink {
  public:
    virtual ~TouchDeviceSink() = default;

    /** @brief Whether the guest driver is active, so a written report reaches it. */
    virtual bool IsActive() const = 0;

    /**
     * @brief Writes `events` followed by `SYN_REPORT` to the device,
     * synchronously and as one report.
     */
    virtual void WriteReport(
            absl::Span<const ::android::emulation::control::EvDevEvent> events) = 0;
};

/**
 * @brief Maps displays to the touch devices that receive their input.
 *
 * ### Thread Safety:
 * Called only on the QEMU event loop.
 *
 * ### Ownership:
 * A sink returned by `Find()` stays valid for as long as the caller uses it;
 * sinks are installed per display when the display is created.
 */
class TouchDevices {
  public:
    virtual ~TouchDevices() = default;

    /**
     * @brief Maps a requested display to the physical device that receives its
     * touches; a foldable may remap display 0 to 1 depending on its posture.
     * Called once when a contact is created.
     */
    virtual absl::StatusOr<TouchDeviceId> Resolve(int32_t display_id) = 0;

    /** @return The sink of `device`, or `nullptr` if there is none. */
    virtual TouchDeviceSink* Find(TouchDeviceId device) = 0;
};

}  // namespace goldfish::grpc::v2
