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

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/time/time.h"

#include "goldfish/async/async_socket_factory.h"
#include "goldfish/async/event_loop.h"
#include "goldfish/http/http_headers.h"
#include "goldfish/http/http_request.h"
#include "goldfish/http/http_response.h"
#include "goldfish/http/http_response_writer.h"
#include "goldfish/http/http_router.h"
#include "goldfish/http/http_session.h"
#include "goldfish/network/endpoint.h"

namespace goldfish::http {

/**
 * @brief Fluent configuration builder for initializing a WebServer instance.
 *
 * @section usage_example Usage Example
 * @code
 * auto config = CreateWebServer(8080)
 *                   .BindAddress("127.0.0.1")
 *                   .MaxPayloadSize(32 * 1024 * 1024)
 *                   .AccessLog([](const AccessLogEntry& e) {
 *                       LOG(INFO) << e.client_endpoint.ToString() << " "
 *                                 << HttpMethodToString(e.method) << " " << e.path << " -> "
 *                                 << e.status_code << " in " << absl::FormatDuration(e.duration);
 *                   });
 * WebServer server{config};
 * @endcode
 */
class CreateWebServer {
  public:
    /**
     * @brief Constructs builder with a listening port (default: 8080, 0 for dynamic assignment).
     */
    explicit CreateWebServer(uint16_t port = 8080);

    /**
     * @brief Sets the TCP listening port.
     */
    CreateWebServer& Port(uint16_t p);

    /**
     * @brief Sets the local IP bind address (e.g. "127.0.0.1", "0.0.0.0", "::").
     */
    CreateWebServer& BindAddress(std::string address);

    /**
     * @brief Sets the maximum permitted request payload in bytes (default: 64MB).
     */
    CreateWebServer& MaxPayloadSize(size_t bytes);

    /**
     * @brief Injects an external EventLoop to share a thread with other async tasks.
     */
    CreateWebServer& EventLoop(async::EventLoop* loop);

    /**
     * @brief Injects a custom socket factory (useful for unit testing with FakeAsyncSocket).
     */
    CreateWebServer& SocketFactory(std::shared_ptr<async::AsyncSocketFactory> factory);

    /**
     * @brief Sets an access logger callback invoked on every completed HTTP request.
     */
    CreateWebServer& AccessLog(AccessLogger logger);

    /**
     * @brief Validates the configuration parameters before initialization.
     * @return absl::OkStatus() if valid, or an InvalidArgumentError explaining the defect.
     */
    [[nodiscard]] absl::Status Validate() const;

    uint16_t Port() const { return port_; }
    const std::string& BindAddress() const { return address_; }
    size_t MaxPayloadSize() const { return max_payload_size_; }
    async::EventLoop* EventLoop() const { return loop_; }
    std::shared_ptr<async::AsyncSocketFactory> SocketFactory() const { return socket_factory_; }
    const AccessLogger& AccessLog() const { return access_logger_; }

  private:
    uint16_t port_ = 8080;
    std::string address_ = "0.0.0.0";
    size_t max_payload_size_ = 64 * 1024 * 1024;
    async::EventLoop* loop_ = nullptr;
    std::shared_ptr<async::AsyncSocketFactory> socket_factory_;
    AccessLogger access_logger_;
};

/// @brief Lightweight, non-blocking HTTP/1.1 web server.
///
/// Provides lambda-first route registration, full Keep-Alive management,
/// and support for unary and streaming HTTP responses.
///
/// Threading Model:
/// - When constructed without an external EventLoop, manages its own dedicated background thread.
/// - Thread-safe route registration, Start, Stop, and ActiveSessionsCount queries.
///
/// @section usage_example Complete Server Example
/// @code
/// #include "goldfish/http/http_headers.h"
/// #include "goldfish/http/web_server.h"
///
/// int main() {
///     using namespace goldfish::http;
///
///     WebServer ws{CreateWebServer(8080).BindAddress("0.0.0.0")};
///
///     // Unary GET
///     ws.OnGet("/hello", [](const HttpRequest& req) {
///         return HttpResponse::String("Hello, World!");
///     });
///
///     // Wildcard CORS Preflight
///     ws.OnOptions("/*", [](const HttpRequest& req) {
///         return HttpResponse::Empty(204)
///             .WithHeader(headers::kAccessControlAllowOrigin, "*")
///             .WithHeader(headers::kAccessControlAllowMethods, "POST, GET, OPTIONS")
///             .WithHeader(headers::kAccessControlAllowHeaders, "content-type,x-grpc-web");
///     });
///
///     // Asynchronous Streaming Route
///     ws.OnStream(HttpMethod::kPost, "/grpc.service.*",
///         [](const HttpRequest& req, std::shared_ptr<HttpResponseWriter> writer) {
///             writer->SendHeaders({{std::string(headers::kContentType),
///                                  std::string(mime::kApplicationGrpcWeb)}});
///             writer->SendChunk("data");
///             writer->Finish();
///         });
///
///     // Start server and block
///     ws.Start(true);
/// }
/// @endcode
class WebServer {
  public:
    explicit WebServer(CreateWebServer config);
    ~WebServer();

    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    /**
     * @brief Registers a GET handler for an exact path or prefix pattern.
     */
    WebServer& OnGet(std::string path, HttpHandler handler);

    /**
     * @brief Registers a POST handler for an exact path or prefix pattern.
     */
    WebServer& OnPost(std::string path, HttpHandler handler);

    /**
     * @brief Registers a PUT handler for an exact path or prefix pattern.
     */
    WebServer& OnPut(std::string path, HttpHandler handler);

    /**
     * @brief Registers a DELETE handler for an exact path or prefix pattern.
     */
    WebServer& OnDelete(std::string path, HttpHandler handler);

    /**
     * @brief Registers an OPTIONS handler for an exact path or prefix pattern.
     */
    WebServer& OnOptions(std::string path, HttpHandler handler);

    /**
     * @brief Registers a streaming / asynchronous handler.
     */
    WebServer& OnStream(HttpMethod method, std::string path, AsyncHttpHandler handler);

    /**
     * @brief Registers a custom handler for 404 Not Found requests.
     */
    void SetNotFoundHandler(HttpHandler handler);

    /**
     * @brief Binds the listening socket and begins accepting HTTP connections.
     * @param blocking If true, blocks the current thread until Stop() is invoked.
     * @return absl::OkStatus() on success, or an error status on failure.
     */
    [[nodiscard]] absl::Status Start(bool blocking = false);

    /**
     * @brief Stops the server listener and cleanly drains active HTTP sessions.
     */
    void Stop();

    /**
     * @brief Returns the local listening endpoint (e.g. 127.0.0.1:8080).
     */
    network::Endpoint GetEndpoint() const;

    /**
     * @brief Returns the number of currently connected HTTP client sessions.
     */
    size_t ActiveSessionsCount() const;

  private:
    struct State;
    std::shared_ptr<State> state_;
};

}  // namespace goldfish::http
