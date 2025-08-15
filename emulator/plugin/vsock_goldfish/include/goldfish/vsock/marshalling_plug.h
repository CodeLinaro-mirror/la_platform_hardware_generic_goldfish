// Copyright (C) 2025 The Android Open Source Project
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
#include <variant>

#include "goldfish/async/event_loop.h"
#include "goldfish/async/qemu_event_loop.h"
#include "goldfish/devices/cable/cable.h"
#include "goldfish/vsock/connect.h"
#include "goldfish/vsock/listen.h"

namespace goldfish {
namespace devices {
namespace cable {

using async::EventLoop;

/**
 * @brief A callback function responsible for handling the unplug logic of a raw socket.
 * @param ISocket* The raw socket pointer (living on the QEMU thread) to be unplugged.
 * @return The PlugPtr that was connected to the socket.
 * @note This is provided for testing and specific integration purposes. VsockStream provides
 * a concrete implementation for "real" virtual sockets.
 */
using UnpluggerFn = std::function<PlugPtr(ISocket*)>;

/**
 * @class MarshallingSocket
 * @brief A thread-safe handle for sending data to a raw QEMU-side socket.
 *
 * This class provides a thread-safe wrapper around a raw socket that lives on
 * the main QEMU event loop. It is designed to be passed to a client running on
 * a separate event loop.
 *
 * @warning This class is a **handle**, not a fully thread-safe object. Only the
 * `sendAsync` method is thread-safe and can be called from any thread. All other
 * methods (`switchPlug`, `setDataSniffer`, `unplugImpl`) are **NOT** thread-safe
 * and **MUST** be called from the QEMU event loop thread. Failure to do so will
 * result in race conditions and undefined behavior.
 */
struct MarshallingSocket : public ISocket {
    /**
     * @brief Constructs a MarshallingSocket.
     * @param socket The raw QEMU-side socket to wrap. This MarshallingSocket
     * instance takes ownership of the provided SocketPtr.
     * @param clientLoop The event loop of the client that will be using this socket.
     * @param unplugger The function to be called on the QEMU thread to handle
     * the unplug logic.
     */
    MarshallingSocket(SocketPtr socket, EventLoop* clientLoop, UnpluggerFn unplugger);

    /**
     * @brief Asynchronously sends data by marshalling the call to the QEMU loop.
     * This method is non-blocking and is the **only** method in this class that is
     * safe to call from any thread.
     * @param data A pointer to the data buffer to send.
     * @param size The size of the data buffer.
     */
    void sendAsync(const void* data, size_t size) override;

    /**
     * @brief Switches the attached plug.
     * @warning This method is **NOT** thread-safe. It **MUST** be called from the
     * QEMU event loop.
     * @param newPlug The new plug to be attached to the underlying socket.
     * @return The previously attached plug.
     */
    PlugPtr switchPlug(PlugPtr newPlug) override;

    /**
     * @brief Sets a data sniffer.
     * @warning This method is **NOT** thread-safe. It **MUST** be called from the
     * QEMU event loop.
     * @param sniffer The sniffer to install on the underlying socket.
     */
    void setDataSniffer(std::unique_ptr<IDataSniffer> sniffer) override;

    /**
     * @brief Executes the unplug logic.
     * @warning This method is **NOT** thread-safe. It **MUST** be called from the
     * QEMU event loop.
     * @return The plug that was detached from the socket.
     */
    PlugPtr unplugImpl() override;

    /// @brief The client's event loop.
    EventLoop* mClientLoop;
    /// @brief The main QEMU event loop where the wrapped socket resides.
    EventLoop* mQemuLoop;
    /// @brief The function that handles the actual unplugging on the QEMU thread.
    UnpluggerFn mUnplugger;
    /// @brief The wrapped raw socket that lives on the QEMU thread.
    cable::SocketPtr mWrappedSocket;

    /**
     * @brief Creates a generic unplugger function for a given socket type.
     *
     * This template function generates an `UnpluggerFn` that safely casts the
     * `ISocket*` to the specified type `T` and calls its `unplugImpl()`
     * method. This is a convenient way to create type-safe unpluggers for
     * concrete socket implementations.
     *
     * @tparam T The concrete socket implementation type (e.g., VsockStream).
     *           This type must inherit from `ISocket` and have an `unplugImpl()`
     *           method.
     * @return An `UnpluggerFn` that can be passed to the `MarshallingSocket`
     *         constructor.
     */
    template <typename T>
    static UnpluggerFn unpluggerFor() {
        return [](devices::cable::ISocket* socket) {
            auto* stream = static_cast<T*>(socket);
            return stream->unplugImpl();
        };
    }
};

/**
 * @class MarshallingPlug
 * @brief An IPlug adapter that marshals `onReceive` calls from the QEMU event
 * loop to a client event loop.
 *
 * This class wraps a client-provided plug to safely handle callbacks from a
 * socket that lives on the QEMU event loop.
 *
 * @warning This class is a thread-crossing adapter, not a fully thread-safe
 * object. Only the `onReceive` callback is marshalled to the client loop. All
 * other callbacks (`onConnect`, `onUnplug`, and snapshot methods) are invoked
 * directly on the **QEMU thread**. Implementations of these client plug methods
 * must themselves be thread-safe or delegate work to the client loop if necessary.
 */
struct MarshallingPlug : public IPlug {
    /**
     * @brief Constructs a MarshallingPlug.
     * @param clientLoop The event loop on which the client's IPlug will operate.
     * @param plug The client's plug instance that will receive events.
     */
    MarshallingPlug(EventLoop* clientLoop, PlugPtr plug);

    /**
     * @brief Receives data on the QEMU loop and asynchronously posts the call
     * to the client plug on the client's event loop. Note that this function
     * will *ALWAYS* return true, informing qemu that the bytes have been accepted.
     *
     * @warning The return value from the wrapped plug will be ignored.
     */
    bool onReceive(const void* data, size_t size) override;

    /**
     * @brief Receives the connect event on the QEMU loop.
     * @warning This callback is executed directly on the **QEMU thread** and is
     * **NOT** marshalled to the client loop.
     */
    void onConnect() override;

    /**
     * @brief Receives the unplug event on the QEMU loop.
     * @warning This callback is executed directly on the **QEMU thread** and is
     * **NOT** marshalled to the client loop.
     */
    SocketPtr onUnplug() override;

    /**
     * @brief Forwards the snapshot support query.
     * @warning This callback is executed directly on the **QEMU thread** and is
     * **NOT** marshalled to the client loop.
     */
    bool supportsLoadingFromSnapshot() const override;

    /**
     * @brief Forwards the snapshot type ID query.
     * @warning This call is executed directly on the QEMU thread and is NOT
     * marshalled to the client loop.
     */
    TypeId getSnapshotTypeId() const override;

    /**
     * @brief Forwards the save state request.
     * @warning This call is executed directly on the QEMU thread and is NOT
     * marshalled to the client loop.
     */
    bool saveStateToSnapshot(archive::IWriter& writer) const override;

  protected:
    /// @brief The client's event loop where the wrapped plug's receive methods are executed.
    EventLoop* mClientLoop;
    /// @brief The main QEMU event loop where this plug receives its events.
    EventLoop* mQemuLoop;
    /// @brief The wrapped client plug instance.
    cable::PlugPtr mClientPlug;
};

/**
 * @brief Listens on a host port, creating connections that marshal send/receive calls.
 *
 * Sets up a vsock listener on the QEMU thread. For each new connection, it calls
 * the provided listener callback on the `clientLoop`. The resulting connection
 * will be set up to marshal `sendAsync` from the client loop to the QEMU
 * loop, and `onReceive` from the QEMU loop to the client loop.
 *
 * @param hostPort The host port to listen on.
 * @param listener The listener callback to be invoked on the clientLoop for new connections.
 * @param clientLoop The event loop on which the listener callback will be executed.
 * @param unplugger A function that handles the unplug logic for a raw socket on the QEMU thread.
 * @return true if listening started successfully, false otherwise.
 */
bool listenWithMarshalling(uint32_t hostPort, vsock::HostPortListener listener,
                           async::EventLoop* clientLoop, UnpluggerFn unplugger);

/**
 * @brief Connects to a guest port, creating a connection that marshals send/receive calls.
 *
 * The provided `clientPlug` will have its `onReceive` method called on the
 * `clientLoop`. The returned `MarshallingSocket` is safe to use for `sendAsync`
 * from the `clientLoop`. All other interactions are not thread-safe.
 *
 * @param guestPort The guest port to connect to.
 * @param clientPlug The plug to connect to the guest. Its `onReceive` will be called
 * on the clientLoop.
 * @param clientLoop The event loop on which the plug's `onReceive` will be called.
 * @param unplugger A function that handles the unplug logic for a raw socket on the QEMU thread.
 * @return A `SocketPtr` with partial marshalling, or `nullptr` if connection fails.
 */
devices::cable::SocketPtr connectWithMarshalling(uint32_t guestPort,
                                                 devices::cable::PlugPtr clientPlug,
                                                 async::EventLoop* clientLoop,
                                                 UnpluggerFn unplugger);

}  // namespace cable
}  // namespace devices
}  // namespace goldfish