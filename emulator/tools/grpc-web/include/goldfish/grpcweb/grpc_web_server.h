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

#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>

#include "absl/status/statusor.h"

#include "goldfish/async/async_socket_factory.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/http/web_server.h"
#include "goldfish/network/endpoint.h"

namespace goldfish::grpcweb {

/**
 * @brief High-performance HTTP server that terminates gRPC-Web client requests
 *        and bridges them directly to an upstream gRPC server using goldfish::http.
 */
class GrpcWebServer {
  public:
    /**
     * @brief Creates and starts listening on the specified endpoint.
     * @param loop EventLoop thread where network I/O is scheduled. Must outlive the server.
     * @param socket_factory Factory to create AsyncSocket and AsyncSocketServer.
     * @param endpoint Network endpoint to bind and listen on (e.g. 0.0.0.0:8080).
     * @param grpc_channel In-process or network gRPC channel to upstream services.
     * @param default_allow_origin Default CORS Access-Control-Allow-Origin header (default "*").
     * @param access_logger Optional callback invoked for every completed HTTP/gRPC-Web request.
     * @param static_handler Optional HTTP GET handler to serve static frontend assets.
     * @return std::unique_ptr to running GrpcWebServer, or status on failure.
     *
     * Ownership: Caller takes ownership of the returned GrpcWebServer instance.
     * Threading: Safe to invoke from any thread.
     * Blocking: Non-blocking.
     */
    static absl::StatusOr<std::unique_ptr<GrpcWebServer>> Create(
            async::EventLoop* loop, std::shared_ptr<async::AsyncSocketFactory> socket_factory,
            const network::Endpoint& endpoint, std::shared_ptr<grpc::Channel> grpc_channel,
            std::string default_allow_origin = "*", http::AccessLogger access_logger = nullptr,
            http::HttpHandler static_handler = nullptr);

    ~GrpcWebServer();

    GrpcWebServer(const GrpcWebServer&) = delete;
    GrpcWebServer& operator=(const GrpcWebServer&) = delete;

    /**
     * @brief Returns the local listening endpoint (useful when binding to port 0).
     */
    network::Endpoint GetEndpoint() const;

    /**
     * @brief Stops listening and closes the server socket.
     *
     * Threading: Safe to invoke from any thread.
     */
    void Stop();

    /**
     * @brief Returns the number of currently active HTTP/gRPC-Web sessions.
     *
     * Threading: Safe to invoke from any thread.
     */
    size_t ActiveSessionsCount() const;

  private:
    explicit GrpcWebServer(std::unique_ptr<http::WebServer> http_server);

    std::unique_ptr<http::WebServer> http_server_;
};

}  // namespace goldfish::grpcweb
