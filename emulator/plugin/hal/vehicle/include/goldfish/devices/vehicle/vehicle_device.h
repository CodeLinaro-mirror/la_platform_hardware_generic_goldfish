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

#include <string_view>

#include "goldfish/async/event_loop.h"
#include "goldfish/avd_universe/vehicle/vehicle_data.h"
#include "goldfish/devices/connector_registry.h"
#include "goldfish/devices/internal/hal_plug.h"

namespace goldfish::devices::vehicle {

using goldfish::async::EventLoop;
using namespace std::string_view_literals;

class IVehicleDevice : public HalPlug {
  public:
    // Name under which you should register this in qemu
    static constexpr std::string_view kServiceName = "vehicle"sv;

    /**
     * @brief Registers the vehicle device with the connector registry.
     *
     * @param channel The physical representation of the vehicle data.
     * @param registry The `IConnectorRegistry` instance to register with.
     * @param client_loop The event loop for client-side operations.
     * @param qemu_loop The event loop for QEMU-side operations.
     */
    static void RegisterDevice(avd_universe::vehicle::VehicleChannel* channel,
                               IConnectorRegistry* registry, EventLoop* client_loop,
                               EventLoop* qemu_loop);
};

}  // namespace goldfish::devices::vehicle
