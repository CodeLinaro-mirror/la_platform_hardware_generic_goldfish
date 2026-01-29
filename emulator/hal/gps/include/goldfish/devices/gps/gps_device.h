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
using namespace std::string_view_literals;

/**
 * The guest HAL implementation resides in `device/generic/goldfish/hals/gnss`.
 */
class IGpsDevice : public HalPlug, public std::enable_shared_from_this<IGpsDevice> {
  public:
    /**
     * @brief QEMU service name for the GPS device.
     */
    static constexpr std::string_view kServiceName = "gps"sv;

    /**
     * @brief Registers the GPS device with the connector registry.
     *
     * This function registers the GPS device with the provided \p registry
     * instance, making it available for connection through the qemud pipe.
     *
     * @param registry The connector registry instance.
     * @param clientLoop The event loop for client-side operations.
     * @param qemu_loop The event loop for QEMU-side operations.
     */
    static void RegisterDevice(ObservableLocation*, IConnectorRegistry* registry,
                               EventLoop* client_loop, EventLoop* qemu_loop);
};

}  // namespace goldfish::devices::gps
