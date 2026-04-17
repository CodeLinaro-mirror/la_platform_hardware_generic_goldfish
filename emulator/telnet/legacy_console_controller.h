// Copyright (C) 2026 The Android Open Source Project
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
#include <string_view>
#include <vector>

#include "absl/status/status.h"

#include "console_server.h"
#include "goldfish/async/async_socket_factory.h"
#include "goldfish/async/event_loop.h"

namespace goldfish::telnet {

class LegacyConsoleBridge;

/**
 * @class LegacyConsoleController
 * @brief Manages the lifecycle of legacy telnet console servers for both IPv4 and IPv6.
 */
class LegacyConsoleController {
  public:
    /**
     * @brief Constructs the controller.
     *
     * @param factory The socket factory used to create listeners.
     * @param main_loop The core thread event loop driving the listener sockets.
     */
    LegacyConsoleController(goldfish::async::AsyncSocketFactory& factory,
                            goldfish::async::EventLoop* main_loop);

    /**
     * @brief Starts the console servers for both IPv4 and IPv6 on the specified port.
     *
     * @param port The port to listen on.
     * @return absl::Status Ok if at least one server started successfully, or an error.
     */
    absl::Status Start(int port);

    /**
     * @brief Stops all running console servers.
     *
     * @return absl::Status Ok if all servers stopped successfully.
     */
    absl::Status Stop();

  private:
    struct ServerInstance {
        std::string name;
        std::shared_ptr<ConsoleServer> server;
    };

    goldfish::async::AsyncSocketFactory& factory_;
    goldfish::async::EventLoop* main_loop_;
    std::shared_ptr<LegacyConsoleBridge> handler_;

    std::vector<ServerInstance> servers_;
};

}  // namespace goldfish::telnet
