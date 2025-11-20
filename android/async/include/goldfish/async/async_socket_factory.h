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
#include "goldfish/network/dns_resolver.h"
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
    virtual std::shared_ptr<AsyncSocket> createSocket(EventLoop* loop,
                                                      const network::Endpoint& endpoint) = 0;

    /**
     * @brief Creates a server socket that listens on a specific, pre-resolved
     * Endpoint.
     *
     * @param loop The EventLoop to associate with the server.
     * @param endpoint The resolved network endpoint to bind to.
     * @param connectCallback The callback that will be invoked for each new
     *                        incoming connection.
     * @return std::shared_ptr<AsyncSocketServer> A new AsyncSocketServer
     *                                            instance, or nullptr if the
     *                                            server could not be started.
     */
    virtual std::shared_ptr<AsyncSocketServer> createServer(
            EventLoop* loop, const network::Endpoint& endpoint,
            AsyncSocketServer::ConnectCallback connectCallback) = 0;
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
std::shared_ptr<AsyncSocket> createSocketFromHostname(AsyncSocketFactory& factory, EventLoop* loop,
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
 * @param connectCallback The callback for new connections.
 * @return std::shared_ptr<AsyncSocketServer> A new server, or nullptr on
 * failure.
 */
std::shared_ptr<AsyncSocketServer> createServerFromHostname(
        AsyncSocketFactory& factory, EventLoop* loop, const std::string& hostname,
        AsyncSocketServer::ConnectCallback connectCallback);

}  // namespace goldfish::async
