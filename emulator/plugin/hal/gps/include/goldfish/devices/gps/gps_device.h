// Copyright 2025 The Android Open Source Project
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

#include <string_view>

#include "goldfish/async/event_loop.h"
#include "goldfish/avd_universe/gps/location.h"
#include "goldfish/devices/connector_registry.h"

namespace goldfish::devices::gps {

using goldfish::async::EventLoop;
using goldfish::avd_universe::gps::ObservableLocation;

/**
 * @brief Host-side QEMUD GPS HAL device implementation.
 *
 * Streams NMEA 0183 location sentences (`$GPGGA` and `$GPRMC`) to the guest
 * GNSS HAL (`device/generic/goldfish/hals/gnss`) over the `"gps"` QEMUD pipe.
 *
 * ### Data Flow
 * - Broadcasts the current location at 1 Hz on a repeating `QemuTimer` bound to
 *   `QEMU_CLOCK_VIRTUAL`, which pauses automatically when the guest VM is
 *   paused or suspended.
 * - Subscribes to `ObservableLocation` and transmits an immediate NMEA update
 *   whenever the host location changes.
 *
 * ### Thread Safety
 * - All `HalPlug` callbacks (`OnConnect`, `OnClose`, `OnReceive`), the 1 Hz
 *   `QemuTimer`, and `ObservableLocation` update tasks execute on `qemu_loop`
 *   (`QemuEventLoop`).
 * - External `ObservableLocation` updates are posted to `qemu_loop` before
 *   locking the device or accessing device state.
 */
class IGpsDevice : public HalPlug {
  public:
    /**
     * @brief QEMU service name for the GPS device.
     */
    static constexpr std::string_view kServiceName = "gps";

    /**
     * @brief Registers the GPS device with the connector registry.
     *
     * Registers the `"gps"` QEMUD service so that both `HalPlug` lifecycle
     * callbacks and periodic 1 Hz NMEA timer ticks execute on \p qemu_loop.
     *
     * @param observable_location Observable location state shared with host
     *        controllers (UI, console, gRPC). Must outlive registered connections.
     * @param registry The connector registry instance.
     * @param qemu_loop The QEMU event loop (`QemuEventLoop`) used for both
     *        `HalPlug` callbacks and the 1 Hz virtual-clock timer.
     */
    static void RegisterDevice(ObservableLocation* observable_location,
                               IConnectorRegistry* registry, EventLoop* qemu_loop);
};

}  // namespace goldfish::devices::gps
