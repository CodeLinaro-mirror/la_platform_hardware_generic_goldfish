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

class HalPlugFactory;
class HalPlugTesting;
class HalPlugToIPlugAdapter;
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

namespace internal {
// A non-functional socket implementation used as a safe null object.
class NullHalSocket : public HalSocket {
  void send(std::string data) override {}
  void close() override {}
};
}  // namespace internal

/**
 * @class HalPlug
 * @brief A simplified, thread-safe plug interface for HAL devices.
 *
 * This class provides a robust, thread-safe abstraction for HALs that need to
 * communicate over a vsock connection. It guarantees that all its methods
 * (`onConnect`, `onReceive`, `onClose`) are invoked on the client-provided
 * event loop, freeing the developer from worrying about QEMU's threading model
 * or potential deadlocks.
 *
 * ### The `socket()` Invariant
 *
 * The most important contract this class provides is the lifecycle of the
 * `HalSocket`.
 *
 * **The pointer returned by `socket()` is only valid for use between the start
 * of the `onConnect()` callback and the start of the `onClose()` callback.**
 *
 * ### Event Sequence
 *
 * Due to the nature of the underlying connection protocol, it is possible for
 * an `onReceive` event to be delivered *before* the `onConnect` event. This
 * first `onReceive` call typically contains connection parameters or arguments
 * from the guest.
 *
 * The guaranteed lifecycle is as follows:
 * 1. **(Optional) `onReceive()`**: One or more calls with initial configuration.
 *    The `socket()` is **not valid** at this point.
 * 2. **`onConnect()`**: Signals that the connection is fully established and
 *    two-way communication is possible. The `socket()` is **now valid**.
 * 3. **`onReceive()`**: Any number of subsequent data packets.
 * 4. **`onClose()`**: Signals that the connection has been terminated. The
 *    `socket()` is **no longer valid** after this call begins.
 *
 * The framework enforces this:
 * - Before `onConnect()` is called, `socket()` will return a safe, non-functional
 *   "null" socket.
 * - After `onClose()` has been called, `socket()` will also return a "null"
 *   socket.
 *
 * This design prevents crashes from use-after-free or null-pointer-dereference
 * errors and makes the connection lifecycle easy to reason about.
 */
class HalPlug {
  public:
    HalPlug() {
        // Start with a safe, non-functional socket. This prevents crashes if
        // the user incorrectly calls socket() before onConnect().
        static auto nullSocket = std::make_shared<internal::NullHalSocket>();
        mSocket = nullSocket;
    }

    virtual ~HalPlug() = default;

    /**
     * @brief Callback invoked when the connection is fully established.
     *
     * This method is called on the HAL's dedicated event loop and signals that
     * two-way communication with the guest is now possible. It is the point
     * at which the `socket()` becomes valid for sending data.
     *
     * @note An `onReceive` callback with initial connection parameters may
     * have been called *before* this method. Any such data should be buffered
     * and processed here.
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
     * @brief Callback invoked when the connection has been terminated by the guest.
     *
     * This method is called on the HAL's dedicated event loop when the remote
     * guest actively closes the connection. After this call begins, the
     * `socket()` will no longer be valid. Implementations should perform any
     * necessary resource cleanup within this method.
     *
     * @note This callback is only triggered by **guest-initiated** disconnects.
     * It will **not** be called as a result of the host calling
     * `socket()->close()`.
     * @note Due to nature of concurrency it is possible that you wrote some bytes to a NullSocket
     * before you received the onClose callback.
     */
    virtual void onClose() = 0;

  protected:
    /**
     * @brief Provides access to the underlying `HalSocket`.
     *
     * @return A pointer to the `HalSocket` instance for this connection.
     * @warning This method adheres to the class invariant: the returned
     * pointer is only functional between the `onConnect()` and `onClose()`
     * calls. At all other times, it will return a safe, non-functional
     * "null" socket.
     */
    std::shared_ptr<HalSocket> socket() { return mSocket; }

  private:
    friend class HalPlugFactory;
    friend class HalPlugToIPlugAdapter;
    friend class HalPlugTesting;

    void establishConnection(std::shared_ptr<HalSocket> socket) { mSocket = std::move(socket); }
    std::shared_ptr<HalSocket> mSocket;
};

}  // namespace devices
}  // namespace goldfish
