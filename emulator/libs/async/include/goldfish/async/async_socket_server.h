// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law jobless or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <functional>
#include <memory>

#include "goldfish/async/scoped_async_resource.h"
#include "goldfish/network/endpoint.h"

namespace goldfish::async {

class AsyncSocket;
class EventLoop;

/**
 * @brief An interface for an asynchronous, event-driven TCP server.
 *
 * This class listens on a port and uses a callback to notify the user of
 * new incoming client connections. Instances are created via an
 * `AsyncSocketFactory`.
 *
 * @warning **Threading Model:** All methods of an `AsyncSocketServer` instance
 * **MUST** be called from the `EventLoop` thread it is associated with,
 * unless otherwise noted.
 *
 * @warning **Object Lifetime:** The `Close()` method must be called before the
 * object is destroyed. The `ScopedAsyncServer` RAII wrapper is the recommended
 * way to manage the server's lifetime automatically and safely.
 *
 * @see ScopedAsyncServer
 * @see AsyncSocketFactory
 */
class AsyncSocketServer {
  public:
    /**
     * @brief Callback invoked for each new incoming client connection.
     *
     * This callback is the primary mechanism for handling new clients. It is
     * executed on the server's event loop thread.
     *
     * This callback **MUST** set the reading callback in `sock` to prevent loss of data.
     * The process will abort intentionally otherwise.
     *
     * @param socket A `std::shared_ptr` to the newly accepted `AsyncSocket`.
     * @return Return `true` to accept the connection, or `false` to
     * immediately reject and close it.
     *
     * @warning **Ownership Requirement:** To accept a connection, you **MUST**
     * store the provided `socket` `std::shared_ptr` in a location that outlives
     * this callback (e.g., in a `ScopedAsyncSocket` or a container). The system
     * verifies this by checking the socket's reference count. If you return
     * `true` but do not store the pointer, the connection will be treated as
     * abandoned and immediately closed.
     */
    using ConnectCallback = std::function<bool(std::shared_ptr<AsyncSocket> socket)>;

    /**
     * @brief Callback invoked to determine which EventLoop should own a new connection.
     *
     * This allows a server to dispatch incoming connections to different threads
     * (e.g., a "thread-per-connection" or "fixed-size-pool" model).
     *
     * @param remote The endpoint of the incoming client.
     * @return The EventLoop pointer that should own the new socket.
     *         Return `nullptr` to keep the connection on the server's loop.
     *
     * @note **Backend Compatibility:** Handoff is only supported between loops
     * derived from the same factory. For example, a `LibuvAsyncSocketFactory`
     * can only hand off connections to other `LibuvEventLoop` instances.
     * Attempting to hand off to a different backend (e.g., Libuv to Qemu)
     * is undefined behavior.
     *
     * @note **Threading Model:** If handoff occurs, the `ConnectCallback` provided
     * to `CreateServer` will be executed on the **target loop's** thread.
     */
    using LoopProvider = std::function<EventLoop*(const network::Endpoint& remote)>;

    virtual ~AsyncSocketServer() = default;

    /**
     * @brief Returns the endpoint the server is listening on.
     * @return The listening endpoint, or an empty address if not listening.
     * @warning This method must be called from the server's event loop thread.
     */
    virtual network::Endpoint GetEndpoint() const = 0;

    /**
     * @brief Initiates the asynchronous closing of the server.
     *
     * This stops the server from accepting new connections and closes the
     * listening socket.
     * @warning This method must be called from the server's event loop thread.
     */
    virtual void Close() = 0;

    /**
     * @brief Returns the EventLoop this server is bound to.
     * @return A non-owning pointer to the event loop.
     * @note This method is thread-safe and is the primary way for an external
     * thread to get the loop pointer needed to `post()` tasks.
     */
    virtual EventLoop* GetLoop() const = 0;
};

/// @brief A convenient RAII wrapper for an `AsyncSocketServer`.
using ScopedAsyncServer = ScopedAsyncResource<AsyncSocketServer>;

}  // namespace goldfish::async
