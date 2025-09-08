/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once
#include <memory>

#include "goldfish/async/event_loop.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/hal/plug/HalPlug.h"

namespace goldfish::devices {
using cable::PlugPtr;
using cable::SocketPtr;
using goldfish::async::EventLoop;

using SnifferFactory = std::function<std::unique_ptr<cable::IDataSniffer>()>;
class HalPlug;

/**
 * @class HalPlugFactory
 * @brief A factory for creating and managing thread-safe HAL plugs.
 *
 * This class provides a set of static methods to establish communication
 * channels for Hardware Abstraction Layer (HAL) devices. It simplifies the
 * process of connecting HALs to the QEMU environment by handling the
 * underlying complexities of vsock connections and thread marshalling.
 *
 * The factory ensures that the user-provided `HalPlug` implementation
 * receives all its events (`onConnect`, `onReceive`, `onClose`) on a
 * dedicated client `EventLoop`, making the HAL implementation robust and free
 * from threading concerns related to QEMU's internal event loop.
 */
class HalPlugFactory {
  public:
    /**
     * @brief A factory function that creates an instance of a HalPlug.
     *
     * This function is called by the framework to instantiate the user's
     * HAL device logic.
     */
    using HalDeviceFactory = std::function<std::shared_ptr<HalPlug>()>;

    /**
     * @brief Wraps an existing QEMU socket with the HalPlug infrastructure.
     *
     * This internal method is used by `listen` to set up the thread-safe
     * communication channel for a newly accepted connection. It creates the
     * necessary adapters and marshalling sockets to bridge the QEMU world
     * with the client's event loop.
     *
     * @param qemuSocket The low-level socket from the QEMU environment.
     * @param halFactory A function to create the user's HalPlug instance.
     * @param clientLoop The event loop for the HalPlug's callbacks.
     * @param qemuLoop The event loop for QEMU-side I/O operations.
     * @return A `PlugPtr` to be managed by the QEMU connection framework.
     */
    static PlugPtr wrapHalPlug(SocketPtr qemuSocket, HalDeviceFactory halFactory,
                               EventLoop* clientLoop, EventLoop* qemuLoop);

    /**
     * @brief Establishes an outbound connection to a vsock service.
     *
     * @param port The vsock port to connect to.
     * @param halFactory A function to create the user's HalPlug instance.
     * @param clientLoop The event loop for the HalPlug's callbacks.
     * @param qemuLoop The event loop for QEMU-side I/O operations.
     * @param dataSnifferFactory A factory that can produce a data sniffer that will be placed on
     * the ISocket.
     * @return A `PlugPtr` representing the connection, or `nullptr` on failure.
     */
    static PlugPtr connect(int port, HalDeviceFactory halFactory, EventLoop* clientLoop,
                           EventLoop* qemuLoop, SnifferFactory dataSnifferFactory = nullptr);

    /**
     * @brief Listens for incoming vsock connections on a specified port.
     *
     * This is the primary method for HALs that act as services. It registers a
     * listener on the given vsock port. For each incoming connection, it
     * instantiates a new `HalPlug` using the provided factory and sets up the
     * thread-safe communication channel.
     *
     *
     * @note You will likely want to use the ConnectorRegistry vs this method.
     * @param port The vsock port to listen on.
     * @param halFactory A factory function to create a `HalPlug` for each new
     * connection.
     * @param clientLoop The event loop where the created `HalPlug` will run.
     * @param qemuLoop The QEMU event loop for handling I/O.
     * @return `true` if the listener was successfully started, `false` otherwise.
     */
    static bool listen(int port, HalDeviceFactory halFactory, EventLoop* clientLoop,
                       EventLoop* qemuLoop);
};
}  // namespace goldfish::devices