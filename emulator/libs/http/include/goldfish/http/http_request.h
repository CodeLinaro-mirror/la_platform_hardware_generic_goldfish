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

#include <optional>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"

#include "goldfish/http/http_headers.h"

namespace goldfish::http {

class HttpSession;

/**
 * @brief Strongly typed enumeration of supported HTTP methods.
 */
enum class HttpMethod : uint8_t {
    kGet,
    kPost,
    kPut,
    kDelete,
    kOptions,
    kHead,
    kPatch,
    kAny,
};

/**
 * @brief Converts an HttpMethod enum value to its uppercase HTTP string representation.
 * @param method The HttpMethod enum value.
 * @return Uppercase method string (e.g. "GET", "POST", "*").
 */
std::string_view HttpMethodToString(HttpMethod method);

/**
 * @brief Parses an HTTP method string into an HttpMethod enum value (case-insensitive).
 * @param method The method string to parse (e.g. "GET", "post", "*").
 * @return HttpMethod enum if recognized, std::nullopt otherwise.
 */
std::optional<HttpMethod> StringToHttpMethod(std::string_view method);

/**
 * @brief Read-only value representation of an incoming HTTP/1.1 request.
 *
 * Provides zero-allocation case-insensitive header lookup and access to the parsed method,
 * path, and payload buffer. Immutable once dispatched to route handlers.
 *
 * @section usage_example Usage Example
 * @code
 * #include "goldfish/http/http_headers.h"
 * #include "goldfish/http/http_request.h"
 *
 * void HandleRequest(const HttpRequest& req) {
 *     if (req.Method() == HttpMethod::kPost) {
 *         auto content_type = req.GetHeader(headers::kContentType);
 *         std::string_view body = req.Body();
 *         // Process body...
 *     }
 * }
 * @endcode
 */
class HttpRequest {
  public:
    using HeaderMap = CaseInsensitiveStringMap<std::string>;

    HttpRequest() = default;

    /**
     * @brief Returns the HTTP method of the request.
     */
    HttpMethod Method() const { return method_; }

    /**
     * @brief Returns the full raw request path including query parameters and fragment (e.g.
     * "/items?id=10").
     */
    std::string_view Path() const { return path_; }

    /**
     * @brief Returns the raw body payload bytes.
     */
    std::string_view Body() const { return body_; }

    /**
     * @brief Returns true if the client requested connection persistence (HTTP/1.1 Keep-Alive).
     */
    bool KeepAlive() const { return keep_alive_; }

    /**
     * @brief Performs a fast, case-insensitive header lookup.
     * @param name Name of the header to find (e.g. headers::kContentType, "Origin",
     * "authorization").
     * @return String view of the header value if found, or empty string_view if not present.
     */
    std::string_view GetHeader(std::string_view name) const;

    /**
     * @brief Checks if a case-insensitive header is present in the request.
     * @param name Name of the header to check.
     * @return true if the header is present, false otherwise.
     */
    bool HasHeader(std::string_view name) const;

    /**
     * @brief Returns the complete map of lowercase headers.
     */
    const HeaderMap& Headers() const { return headers_; }

  private:
    friend class HttpSession;
    friend class HttpRequestTestPeer;

    std::string path_;
    HeaderMap headers_;
    std::string body_;
    HttpMethod method_ = HttpMethod::kGet;
    bool keep_alive_ = true;
};

}  // namespace goldfish::http
