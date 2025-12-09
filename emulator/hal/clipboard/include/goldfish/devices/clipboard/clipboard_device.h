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

#include "goldfish/async/event_loop.h"
#include "goldfish/avd_universe/clipboard/clipboard_data.h"
#include "goldfish/devices/connector_registry.h"

namespace goldfish::devices::clipboard {

using goldfish::async::EventLoop;
using namespace std::string_view_literals;

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
 * The guest side is in com/android/server/clipboard/EmulatorClipboardMonitor.java
 */
class IClipboardDevice : public HalPlug {
  public:
    // Name under which you should register this in qemu
    static constexpr std::string_view serviceName = "clipboard"sv;

    /**
     * @brief Registers the clipboard device with the connector registry.
     *
     * This function registers the clipboard device with the provided
     * `IConnectorRegistry` instance, making it available for connection
     * through the qemud pipe. The `clientLoop` and `qemuLoop` manage the
     * asynchronous operations.
     *
     * @param channel The physical representation of the clipboard.
     * @param registry The `IConnectorRegistry` instance to register with.
     * @param clientLoop The event loop for client-side operations.
     * @param qemuLoop The event loop for QEMU-side operations.
     *
     * @note The `clientLoop`, and `qemuLoop` objects are expected to
     * remain valid for the lifetime of the registry. Their lifecycles should be
     * managed externally to ensure they outlive the registry.
     *
     * @note A clipboard device is not a qemud device.
     */
    static void registerDevice(avd_universe::clipboard::ClipboardChannel* channel,
                               IConnectorRegistry* registry, EventLoop* clientLoop,
                               EventLoop* qemuLoop);
};
}  // namespace goldfish::devices::clipboard