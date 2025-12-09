
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

#include <functional>
#include <memory>

#include "goldfish/async/event_loop.h"
#include "goldfish/devices/PingTopic.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/hal/plug/HalPlug.h"

namespace goldfish {
namespace devices {

using DeviceFactory = std::function<cable::PlugPtr(cable::SocketPtr socket,
                                                   const std::shared_ptr<PingTopic>& pingTopic,
                                                   std::string_view args)>;

/**
 * @brief A factory function for creating a HalPlug instance.
 *
 * This function is invoked by the ConnectorRegistry to create a new instance
 * of a HAL device when a guest connects.
 *
 * @warning The factory function itself is executed on the **QEMU main loop
 * thread**. It is critical that this function be **non-blocking** and that the
 * `HalPlug`'s constructor be lightweight. Any significant work should be
 * deferred to the `onConnect` method. Furthermore, the `HalPlug` instance is
 * not fully initialized at this stage; the `socket()` method is not yet valid.
 * **You MUST NOT call `socket()` or other methods on the `HalPlug` from
 * within this factory function.**
 *
 * @return A `std::shared_ptr` to the newly created HalPlug instance.
 */
using HalDeviceFactory = std::function<std::shared_ptr<HalPlug>()>;

struct IConnectorRegistry {
    virtual ~IConnectorRegistry() = default;

    /**
     * @brief Registers a QEMU device with the registry.
     *
     * @deprecated This method uses the legacy, non-thread-safe IPlug model.
     * Prefer using `registerHalQemuDevice` with the modern `HalPlug` model for
     * all new development.
     *
     * This method registers devices that use the older "qemud" protocol, which
     * has some differences compared to the standard protocol used by
     * `registerDevice()`. The "qemud" protocol involves a specific data format
     * and handshake mechanism.
     *
     * @see https://android.googlesource.com/platform/external/qemu/+/master/docs/ANDROID-QEMUD.TXT
     *      for a detailed description of the "qemud" protocol.
     *
     * @param name The name of the device.
     * @param factory The factory function for creating the device.
     * @return `true` if the device was registered successfully, `false` otherwise.
     */
    [[deprecated("Use registerHalQemuDevice instead.")]] virtual bool registerQemuDevice(
            std::string_view name, DeviceFactory factory) = 0;

    /**
     * @brief Registers a device with the registry.
     *
     * @deprecated This method uses the legacy, non-thread-safe IPlug model.
     * Prefer using `registerHalDevice` with the modern `HalPlug` model for all
     * new development.
     *
     * @param name The name of the device.
     * @param factory The factory function for creating the device.
     * @return `true` if the device was registered successfully, `false` otherwise.
     */
    [[deprecated("Use registerHalDevice instead.")]] virtual bool registerDevice(
            std::string_view name, DeviceFactory factory) = 0;

    /**
     * @brief Registers a thread-safe HAL device with the registry.
     *
     *
     * This method registers a HAL device that is designed to run on a separate
     * event loop. It uses a factory to create the device and transparently
     * wraps it in the necessary marshalling infrastructure to ensure thread-safe
     * communication between the QEMU main loop and the device's event loop.
     *
     * @param name The name of the device.
     * @param clientLoop The event loop on which the device will run.
     * @param qemuLoop The event loop which is tied to qemu.
     * @param factory The factory function for creating the device.
     */
    virtual void registerHalDevice(std::string name, async::EventLoop* clientLoop,
                                   async::EventLoop* qemuLoop, HalDeviceFactory factory) = 0;

    virtual void registerHalQemuDevice(std::string name, async::EventLoop* clientLoop,
                                       async::EventLoop* qemuLoop, HalDeviceFactory factory) = 0;

    IConnectorRegistry() = default;
    IConnectorRegistry(const IConnectorRegistry&) = delete;
    IConnectorRegistry(IConnectorRegistry&&) = delete;
    IConnectorRegistry& operator=(const IConnectorRegistry&) = delete;
    IConnectorRegistry& operator=(IConnectorRegistry&&) = delete;
};

}  // namespace devices
}  // namespace goldfish
