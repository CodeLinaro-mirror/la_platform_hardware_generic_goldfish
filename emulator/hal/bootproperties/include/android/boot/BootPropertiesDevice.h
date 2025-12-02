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
#include <unordered_map>

#include "absl/container/flat_hash_map.h"

#include "BootPropertyString.h"
#include "aemu/base/events/EventSources.h"
#include "android/boot/BootPropertyString.h"
#include "goldfish/devices/connector_registry.h"
#include "goldfish/hal/common/emulator_reset.h"
#include "goldfish/hal/plug/HalPlug.h"

namespace goldfish::devices::boot {

using android::base::eventing::CallbackEventSource;
using goldfish::async::EventLoop;
using goldfish::devices::BootPropertyString;
using goldfish::devices::LimitedString;

using namespace std::string_view_literals;

struct BootPropertyStatus {
    bool dataPartitionMounted{false};
};

/**
 * @brief Interface for reporting boot properties to the guest after the data
 * partition is mounted.
 *
 * This interface facilitates communication between the emulator and the guest
 * operating system to provide boot properties after the guest's data partition
 * has been mounted.  The guest uses these properties for various
 * initialization and configuration tasks.  The implementation of this
 * interface within the emulator interacts with the `qemud` service to
 * transmit the properties to the guest.  The corresponding HAL implementation
 * resides in the Android source tree.
 *
 * The guest HAL lives in device/generic/goldfish/qemu-props/qemu-props.cpp
 *
 * Example usage:
 *
 * ```c++
 * class MyBootPropertyStatusListener : public EventListener<BootPropertyStatus> {
 * public:
 *     void eventArrived(const BootPropertyStatus& status) override {
 *         // Do something..
 *     }
 * };
 *
 * MyBootPropertyStatusListener listener;
 * bootPropertiesDevice->addListener(&listener);
 * // ... later
 * bootPropertiesDevice->removeListener(&listener);
 *
 * // Alternatively, using callbacks:
 * auto callbackId = bootPropertiesDevice->addCallback(
 *     [](const BootPropertyStatus& status) {
 *         // Process status updates.
 *     });
 * // ... later
 * bootPropertiesDevice->removeCallback(callbackId);
 *
 * ```
 */
class IBootPropertiesDevice : public HalPlug, public CallbackEventSource<BootPropertyStatus> {
  public:
    virtual ~IBootPropertiesDevice() override {}

    /**
     * @brief QEMU service name for the bootproperties device.
     */
    static constexpr std::string_view serviceName = "boot-properties"sv;

    /**
     * @brief Checks if the data partition is mounted within the guest. This will
     * return true once the guest has requested the boot properties.
     *
     * @return True if the data partition is mounted; false otherwise.
     */
    virtual bool isDataPartitionMounted() = 0;

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
    static void registerDevice(IConnectorRegistry* registry, Properties properties,
                               EmulatorResetCallbacks resetCallbacks, EventLoop* clientLoop,
                               EventLoop* qemuLoop);
};

// User-defined literal for creating PropertyName objects.
IBootPropertiesDevice::PropertyName operator""_bps(const char* c_str, size_t len);
}  // namespace goldfish::devices::boot