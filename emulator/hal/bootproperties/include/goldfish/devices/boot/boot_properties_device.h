// Copyright 2024 The Android Open Source Project
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

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"

#include "android/status/status_macros.h"
#include "goldfish/devices/connector_registry.h"
#include "goldfish/devices/internal/hal_plug.h"
#include "goldfish/devices/boot/boot_property_string.h"

namespace goldfish::devices::boot {

using goldfish::async::EventLoop;

using namespace std::string_view_literals;

/**
 * @brief Interface for reporting boot properties
 * (see `IBootPropertiesDevice::Properties`) to the guest.
 *
 * This is deprecated and should be replaced with the bootconfig which we already have.
 */
class IBootPropertiesDevice : public HalPlug {
  public:
    /**
     * @brief QEMU service name for the bootproperties device.
     */
    static constexpr std::string_view serviceName = "boot-properties"sv;

    /**
     * @brief Maximum allowed length for a property name.
     * This value must match the corresponding definition in the Android source tree
     * (system/core/include/cutils/properties.h).
     */
    static constexpr int PROPERTY_MAX_NAME = 32;

    /**
     * @brief Maximum allowed length for a property value.
     * This value must match the corresponding definition in the Android source tree
     * (system/core/include/cutils/properties.h).
     */
    static constexpr int PROPERTY_MAX_VALUE = 92;

    // A string of max 32 chars that does not contain
    using PropertyName = BootPropertyString<IBootPropertiesDevice::PROPERTY_MAX_NAME>;
    using PropertyValue = LimitedString<IBootPropertiesDevice::PROPERTY_MAX_VALUE>;
    using Properties = absl::flat_hash_map<PropertyName, PropertyValue>;

    static absl::StatusOr<Properties> make_properties(absl::flat_hash_map<std::string, std::string> string_map) {
      Properties p;
      for (const auto &[n, v]: string_map) {
        ASSIGN_OR_RETURN(auto pn, PropertyName::create(n));
        ASSIGN_OR_RETURN(auto pv, PropertyValue::create(v));
        p[std::move(pn)] = std::move(pv);
      }
      return p;
    }

    /**
     * @brief Registers the boot properties device with the connector registry.
     *
     * This makes the device accessible via qemud.  The initial
     * properties are passed to the device and sent to the guest upon
     * connection.
     *
     * @param registry The connector registry instance.
     * @param properties The set of properties to register.
     * @param resetCallbacks The struct containing register/unregister functions.
     */
    static void RegisterDevice(IConnectorRegistry* registry, Properties properties,
                               EventLoop* client_loop, EventLoop* qemu_loop);
};

}  // namespace goldfish::devices::boot
