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
#include "goldfish/async/async_socket_factory.h"

namespace goldfish::async {

/**
 * @class QemuSocketFactory
 * @brief A factory for creating QEMU-based asynchronous sockets.
 *
 * This class implements the `AsyncSocketFactory` interface to provide sockets
 * that are compatible with the QEMU main loop. It is the entry point for
 * creating both client and server sockets that will be managed by a
 * `QemuEventLoop`.
 *
 * NOTE: These sockets SHOULD ONLY be used by qemu drivers.
 */
class QemuSocketFactory : public AsyncSocketFactory {
  public:
    /**
     * @brief Creates a new asynchronous client socket.
     *
     * This method establishes a connection to the specified address and returns
     * an `AsyncSocket` instance for communication. The socket will be managed
     * by the provided `EventLoop`. The sockets can only connect to 'localhost'.
     *
     * @param loop The event loop that will manage the socket's I/O.
     * @param address The address to connect to, in "ip:port" format.
     * @return A `std::shared_ptr<AsyncSocket>` on success, or `nullptr` on
     *         failure.
     */
    std::shared_ptr<AsyncSocket> createSocket(EventLoop* loop, const std::string& address) override;

    /**
     * @brief Creates a new asynchronous server socket.
     *
     * This method creates a server socket that listens for incoming connections
     * on the specified address. When a new connection is accepted, the
     * `onConnect` callback is invoked. The server can only listen on 'localhost'.
     *
     * @param loop The event loop that will manage the server's I/O, this should be an event loop
     * obtained by `EventLoop* getQemuEventLoop()`
     * @param listenOn The address to listen on, in "ip:port" format.
     * @param onConnect A callback function that is invoked for each new
     *                  connection.
     * @return A `std::shared_ptr<AsyncSocketServer>` on success, or `nullptr`
     *         on failure.
     */
    std::shared_ptr<AsyncSocketServer> createServer(
            EventLoop* loop, const std::string& listenOn,
            AsyncSocketServer::ConnectCallback onConnect) override;
};

}  // namespace goldfish::async
