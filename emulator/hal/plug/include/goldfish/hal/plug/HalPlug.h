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
#include <string>
#include <string_view>

namespace goldfish {
namespace devices {

/**
 * @class HalSocket
 * @brief A simplified, thread-safe socket handle for HALs.
 *
 * This interface is provided to a HalPlug to allow it to send data to the
 * guest and manage the connection lifetime from its own event loop.
 */
class HalSocket {
  public:
    virtual ~HalSocket() = default;

    /**
     * @brief Asynchronously sends a message to the guest.
     *
     * The implementation is responsible for marshalling this call to the
     * correct (QEMU) thread.
     * @note There are no guarantees that the data arrives in the guest.
     * @param data The message to send.
     */
    virtual void send(std::string data) = 0;

    /**
     * @brief Asynchronously closes the connection.
     *
     * The implementation is responsible for marshalling this call to the
     * correct (QEMU) thread.
     */
    virtual void close() = 0;
};

/**
 * @class HalPlug
 * @brief A simplified, deadlock-free plug interface for HAL devices.
 *
 * Implementations of this interface will have their methods invoked on their
 * designated client event loop, not on the main QEMU thread. The base class
 * manages the socket lifetime.
 */
class HalPlug {
  public:
    virtual ~HalPlug() = default;

    /**
     * @brief Establishes the communication channel for this plug.
     *
     * @internal
     * This method is intended for framework use only and should not be called
     * by HAL implementations. It is invoked by the connection adapter to
     * provide the `HalSocket` handle.
     *
     * @param socket A unique pointer to the thread-safe socket for this
     * connection.
     */
    void establishConnection(std::unique_ptr<HalSocket> socket) { mSocket = std::move(socket); }

    /**
     * @brief Callback invoked when a connection from the guest is established.
     *
     * This method is guaranteed to be called on the HAL's dedicated event loop
     * before any calls to `onReceive` or `onClose`. It signals that the
     * `socket()` is now valid and can be used to send data to the guest.
     *
     * @note Any initialization logic that requires sending data to the guest
     * should be performed here.
     */
    virtual void onConnect() = 0;

    /**
     * @brief Callback invoked when data is received from the guest.
     *
     * This method is called on the HAL's dedicated event loop for each
     * incoming data packet.
     *
     * @param data A view of the received data buffer.
     * @warning The `data` parameter is a `std::string_view` and is only valid
     * for the duration of this function call. If the data needs to be
     * stored or used later, it must be copied.
     */
    virtual void onReceive(std::string_view data) = 0;

    /**
     * @brief Callback invoked when the connection has been terminated.
     *
     * After this call, the `socket()` is no longer valid. Implementations should perform any
     * necessary resource cleanup within this method.
     */
    virtual void onClose() = 0;

  protected:
    /**
     * @brief Provides access to the underlying `HalSocket`.
     *
     * @return A pointer to the `HalSocket` instance for this connection.
     * @warning This method should only be called from the HAL's dedicated
     * event loop (e.g., within `onConnect`, `onReceive`, or other
     * posted tasks).
     * @note The returned pointer is only valid after
     * `onConnect()` has been called and before `onClose()` is called.
     * Accessing the socket outside of this lifecycle results in
     * undefined behavior.
     */
    HalSocket* socket() { return mSocket.get(); }

  private:
    std::unique_ptr<HalSocket> mSocket;
};

}  // namespace devices
}  // namespace goldfish
