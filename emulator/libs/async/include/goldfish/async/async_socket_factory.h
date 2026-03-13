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

#include <memory>
#include <string>

#include "goldfish/async/async_socket.h"
#include "goldfish/async/async_socket_server.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/network/endpoint.h"

namespace goldfish::async {
/**
 * @brief An abstract factory for creating network-related objects.
 *
 * Concrete implementations of this factory will provide objects
 * for a specific backend (like libuv, or looper based sockets)
 */
class AsyncSocketFactory {
  public:
    virtual ~AsyncSocketFactory() = default;

    /**
     * @brief Creates a client socket using a specific, pre-resolved Endpoint.
     *
     * @param loop The EventLoop to associate with the socket.
     * @param endpoint The resolved network endpoint to connect to.
     * @return std::shared_ptr<AsyncSocket> A new AsyncSocket instance, or
     *                                      nullptr if the connection could not
     *                                      be initiated.
     */
    virtual std::shared_ptr<AsyncSocket> CreateSocket(EventLoop* loop,
                                                      const network::Endpoint& endpoint) = 0;

    /**
     * @brief Creates a server socket that listens on a specific, pre-resolved
     * Endpoint.
     *
     * @param loop The EventLoop to associate with the server's listening socket.
     * @param endpoint The resolved network endpoint to bind to.
     * @param connect_callback The callback that will be invoked for each new
     *                        incoming connection.
     * @param loop_provider Optional callback to select a target EventLoop for each
     *                     new connection.
     *
     * @section handoff_logic Loop Handoff Behavior
     * If `loop_provider` is provided and returns a non-null EventLoop:
     * 1. The socket is accepted on the server's `loop`.
     * 2. The socket is "handed off" to the target loop.
     * 3. **CRITICAL:** The `connect_callback` will be executed on the **target loop's**
     *    thread, not the server's thread.
     *
     * If `loop_provider` is null or returns `nullptr`, the `connect_callback` runs
     * on the server's `loop`.
     *
     * @return std::shared_ptr<AsyncSocketServer> A new AsyncSocketServer
     *                                            instance, or nullptr if the
     *                                            server could not be started.
     */
    virtual std::shared_ptr<AsyncSocketServer> CreateServer(
            EventLoop* loop, const network::Endpoint& endpoint,
            AsyncSocketServer::ConnectCallback connect_callback,
            AsyncSocketServer::LoopProvider loop_provider) = 0;

    /**
     * @brief Legacy helper to create a server socket using the server's own loop.
     */
    std::shared_ptr<AsyncSocketServer> CreateServer(
            EventLoop* loop, const network::Endpoint& endpoint,
            AsyncSocketServer::ConnectCallback connect_callback) {
        return CreateServer(loop, endpoint, std::move(connect_callback), nullptr);
    }
};

/**
 * @brief Helper to create a client socket from a hostname.
 *
 * This function encapsulates the full resolve-and-connect logic. It attempts
 * to connect to each resolved Endpoint in order until one succeeds.
 *
 * @param factory The factory to use for creating the socket.
 * @param loop The EventLoop to associate with the socket.
 * @param hostname The hostname to connect to.
 * @return std::shared_ptr<AsyncSocket> A new socket, or nullptr on failure.
 */
std::shared_ptr<AsyncSocket> CreateSocketFromHostname(AsyncSocketFactory& factory, EventLoop* loop,
                                                      const std::string& hostname);

/**
 * @brief Helper to create a server from a hostname.
 *
 * This function encapsulates the full resolve-and-bind logic. It attempts to
 * bind to each resolved Endpoint in order until one succeeds.
 *
 * @param factory The factory to use for creating the server.
 * @param loop The EventLoop to associate with the server.
 * @param hostname The hostname to bind to.
 * @param connect_callback The callback for new connections.
 * @return std::shared_ptr<AsyncSocketServer> A new server, or nullptr on
 * failure.
 */
std::shared_ptr<AsyncSocketServer> CreateServerFromHostname(
        AsyncSocketFactory& factory, EventLoop* loop, const std::string& hostname,
        const AsyncSocketServer::ConnectCallback& connect_callback);

}  // namespace goldfish::async
