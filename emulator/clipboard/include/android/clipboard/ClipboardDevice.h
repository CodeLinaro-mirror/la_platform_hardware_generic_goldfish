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

#include "aemu/base/async/Looper.h"
#include "android/emulation/control/utils/CallbackEventSupport.h"
#include "android/goldfish/config/avd.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/devices/connector_registry.h"

namespace goldfish::devices::clipboard {

using android::base::Looper;
using android::emulation::control::EventChangeSupport;
using android::emulation::control::WithCallbacks;
using android::goldfish::Avd;
using goldfish::devices::cable::IPlug;
using goldfish::devices::cable::PlugPtr;
using goldfish::devices::cable::SocketPtr;

using ClipboardData = std::string_view;

/**
 * @brief Interface for interacting with the Android clipboard emulation.
 *
 * This interface defines the methods for communicating with the Android
 * clipboard emulation service, allowing for setting and retrieving clipboard
 * data.  It uses a simple protocol for data exchange, transmitting a 32-bit
 * little-endian size value followed by the data itself.
 *
 * The communication flow involves sending and receiving messages containing
 * clipboard data.  The protocol is as follows:
 *
 *     <uint32_t (LITTLE_ENDIAN)> size | <char[size]> data
 *
 * These messages can be sent from both the guest and the host. Note that
 * the current implementation only supports text data.
 *
 * To receive notifications when the guest updates the clipboard, you can register a callback or an
 * event listener.
 *
 * **1. Using a Callback:**
 *
 * ```c++
 * // Assuming 'clipboardDevice' is a valid pointer to an IClipboardDevice instance.
 * auto callbackId = clipboardDevice->addCallback(
 *     [](const ClipboardData& data) {
 *         // This lambda function will be called when the clipboard data changes.
 *         printf("Clipboard updated: %.*s\n", data.size(), data.data());
 *     });
 *
 * // ... later, to remove the callback:
 * clipboardDevice->removeCallback(callbackId);
 * ```
 *
 * **2. Using an Event Listener:**
 *
 * ```c++
 * class MyClipboardListener : public EventListener<ClipboardData> {
 * public:
 *     void eventArrived(const ClipboardData& data) override {
 *         printf("Clipboard updated: %.*s\n", data.size(), data.data());
 *     }
 * };
 *
 * MyClipboardListener listener;
 * clipboardDevice->addListener(&listener);
 *
 * // ... later, to remove the listener:
 * clipboardDevice->removeListener(&listener);
 * ```
 *
 * The guest side is in com/android/server/clipboard/EmulatorClipboardMonitor.java
 */
class IClipboardDevice : public IPlug, public WithCallbacks<EventChangeSupport, ClipboardData> {
  public:
    ~IClipboardDevice() override {}

    /**
     * @brief Checks if the clipboard device is enabled.
     * @return True if enabled, false otherwise.
     */
    virtual bool isEnabled() const = 0;

    /**
     * @brief Enables or disables the clipboard device.
     * @param enable True to enable, false to disable.
     */
    virtual void enable(bool enable) = 0;

    /**
     * @brief Sets the clipboard contents.
     *
     * This method sends the given contents to the guest using the clipboard
     * protocol. The data is prefixed with its size as a little-endian 32-bit
     * integer and send to the guest.
     *
     * @param contents The clipboard data to set.
     */
    virtual void setContents(ClipboardData contents) = 0;

    /**
     * @brief Gets the current clipboard contents.
     *
     * This method retrieves the latest clipboard data received from the guest.  It does *not*
     * return the data set by `setContents()`.
     *
     * @return The current clipboard data.
     */
    virtual ClipboardData getContents() const = 0;

    /**
     * @brief Registers the clipboard device with the connector registry.
     *
     * This function registers the clipboard device with the provided
     * `IConnectorRegistry` instance, making it available for connection
     * through the qemud pipe.  The provided `Avd` object supplies
     * configuration information for the clipboard device, while the `Looper`
     * instance manages the event loop for asynchronous operations.
     *
     * @param registry The `IConnectorRegistry` instance to register with.
     * @param avd The `Avd` object containing the AVD configuration.
     * @param looper The `Looper` instance to use for asynchronous operations.
     *
     * @note The `avd` and `looper` objects are expected to remain valid for
     * the lifetime of the registry.  Their lifecycles should be managed
     * externally to ensure they outlive the registry.
     *
     * @note A clipboard device is not a qemud device.
     */
    static void registerDevice(IConnectorRegistry* registry, Avd* avd, Looper* looper);
};
}  // namespace goldfish::devices::clipboard