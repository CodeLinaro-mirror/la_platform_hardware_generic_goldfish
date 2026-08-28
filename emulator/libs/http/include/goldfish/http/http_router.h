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
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

#include "goldfish/http/http_headers.h"
#include "goldfish/http/http_request.h"
#include "goldfish/http/http_response.h"
#include "goldfish/http/http_response_writer.h"

namespace goldfish::http {

/**
 * @brief Synchronous request handler callback returning an HttpResponse.
 */
using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

/**
 * @brief Asynchronous / streaming request handler callback operating on an HttpResponseWriter.
 */
using AsyncHttpHandler =
        std::function<void(const HttpRequest&, std::shared_ptr<HttpResponseWriter>)>;

/**
 * @brief Represents a resolved route handler (either unary or streaming).
 */
struct RouteHandler {
    enum class Kind { kUnary, kStreaming };

    Kind kind = Kind::kUnary;
    HttpHandler unary_handler;
    AsyncHttpHandler streaming_handler;
};

/// @brief Deterministic router matching HTTP methods and paths against registered handlers.
///
/// Supports zero-allocation exact path lookups (O(1)), longest-prefix wildcard patterns
/// ("/*", "/static/*"), and allowed method inspection for RFC-compliant 405 Method Not Allowed
/// error responses.
///
/// @section usage_example Usage Example
/// @code
/// HttpRouter router;
///
/// // Exact path unary route
/// router.AddRoute(HttpMethod::kGet, "/api/status", [](const HttpRequest& req) {
///     return HttpResponse::String("OK");
/// });
///
/// // Prefix wildcard route (longest prefix takes precedence)
/// router.AddRoute(HttpMethod::kGet, "/static/*", [](const HttpRequest& req) {
///     return HttpResponse::String("Asset");
/// });
///
/// // Streaming route
/// router.AddStreamingRoute(HttpMethod::kPost, "/stream", [](const HttpRequest& req, auto writer) {
///     writer->SendChunk("data");
///     writer->Finish();
/// });
/// @endcode
class HttpRouter {
  public:
    HttpRouter() = default;

    /// @brief Registers a unary synchronous route handler.
    /// @param method HTTP verb (e.g. HttpMethod::kGet).
    /// @param path Exact path ("/api/v1") or wildcard pattern ("/static/*").
    /// @param handler Unary handler callback.
    void AddRoute(HttpMethod method, std::string path, HttpHandler handler);

    /// @brief Registers an asynchronous / streaming route handler.
    /// @param method HTTP verb (e.g. HttpMethod::kPost).
    /// @param path Exact path ("/api/stream") or wildcard pattern ("/grpc.*").
    /// @param handler Streaming handler callback.
    void AddStreamingRoute(HttpMethod method, std::string path, AsyncHttpHandler handler);

    /**
     * @brief Matches an incoming HTTP method and path against registered routes.
     * @param method The HTTP method verb.
     * @param path The request path to match (without query parameters).
     * @return Resolved RouteHandler if matched, std::nullopt otherwise.
     */
    std::optional<RouteHandler> Match(HttpMethod method, std::string_view path) const;

    /**
     * @brief Returns a sorted list of allowed HTTP method names for a given path.
     *
     * Used to populate the "Allow:" header in 405 Method Not Allowed responses.
     *
     * @param path Request path to check.
     * @return Sorted vector of method strings (e.g. {"GET", "POST"}).
     */
    std::vector<std::string> GetAllowedMethods(std::string_view path) const;

  private:
    struct WildcardRoute {
        HttpMethod method;
        std::string prefix;
        RouteHandler handler;
    };

    void AddRouteHandler(HttpMethod method, std::string path, RouteHandler rh);

    TransparentStringMap<absl::flat_hash_map<HttpMethod, RouteHandler>> exact_routes_;
    std::vector<WildcardRoute> wildcard_routes_;
    TransparentStringMap<absl::flat_hash_set<HttpMethod>> methods_by_exact_path_;
};

}  // namespace goldfish::http
