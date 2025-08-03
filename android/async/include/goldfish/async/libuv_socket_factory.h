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

#include "async_socket.h"
#include "goldfish/async/async_socket_factory.h"

namespace goldfish::async {

class LibuvAsyncSocketFactory : public AsyncSocketFactory {
  public:
    ~LibuvAsyncSocketFactory() = default;

    /**
     * @brief Creates a server instance for the libuv backend.
     */
    std::shared_ptr<AsyncSocketServer> createServer(
            EventLoop* loop, const std::string& address,
            AsyncSocketServer::ConnectCallback connectCallback) override;

    /**
     * @brief Creates a client socket instance for the libuv backend.
     */
    std::shared_ptr<AsyncSocket> createSocket(EventLoop* loop, const std::string& address) override;
};
}  // namespace goldfish::async