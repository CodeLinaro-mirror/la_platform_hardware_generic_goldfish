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

#include <string>
#include <string_view>
#include <utility>

#include "absl/container/flat_hash_map.h"

#include "goldfish/http/http_headers.h"

namespace goldfish::http {

/**
 * @brief Value-typed representation of an outgoing HTTP/1.1 response.
 *
 * Provides fluent builder methods and factory constructors for common response shapes.
 *
 * @section usage_example Usage Example
 * @code
 * // Strongly typed unary string response
 * return HttpResponse::String("Hello, World!", HttpStatus::kOk, mime::kTextPlain);
 *
 * // JSON response with custom headers
 * return HttpResponse::String("{\"status\":\"ok\"}", HttpStatus::kOk, mime::kApplicationJson)
 *     .WithHeader(headers::kAccessControlAllowOrigin, "*")
 *     .WithHeader(headers::kCacheControl, "no-cache");
 *
 * // Empty CORS preflight response
 * return HttpResponse::Empty(HttpStatus::kNoContent)
 *     .WithHeader(headers::kAccessControlAllowOrigin, "*")
 *     .WithHeader(headers::kAccessControlAllowMethods, "POST, GET, OPTIONS");
 * @endcode
 */
class HttpResponse {
  public:
    /**
     * @brief Creates a response containing a text or binary payload using a strongly typed
     * HttpStatus.
     * @param body Raw response payload data.
     * @param status Strongly typed HttpStatus enum (default: HttpStatus::kOk).
     * @param content_type MIME media type for the Content-Type header (default: "text/plain").
     */
    static HttpResponse String(std::string body, HttpStatus status = HttpStatus::kOk,
                               std::string_view content_type = mime::kTextPlain);

    /**
     * @brief Creates a response containing a text or binary payload using a raw integer status
     * code.
     * @param body Raw response payload data.
     * @param status Integer HTTP status code.
     * @param content_type MIME media type for the Content-Type header (default: "text/plain").
     */
    static HttpResponse String(std::string body, int status,
                               std::string_view content_type = mime::kTextPlain);

    /**
     * @brief Creates an empty body response with a strongly typed HttpStatus.
     * @param status Strongly typed HttpStatus (default: HttpStatus::kNoContent).
     */
    static HttpResponse Empty(HttpStatus status = HttpStatus::kNoContent);

    /**
     * @brief Creates an empty body response with a raw integer status code.
     * @param status Integer HTTP status code.
     */
    static HttpResponse Empty(int status);

    HttpResponse() = default;

    /**
     * @brief Fluent mutator to set the HTTP status code via HttpStatus enum.
     */
    HttpResponse& WithStatus(HttpStatus status);

    /**
     * @brief Fluent mutator to set the HTTP status code via raw integer.
     */
    HttpResponse& WithStatus(int status);

    /**
     * @brief Fluent mutator to attach or replace a response header.
     * @param name Header name (e.g. headers::kContentType or "X-Custom-Header").
     * @param value Header value.
     */
    HttpResponse& WithHeader(std::string_view name, std::string_view value);

    /**
     * @brief Fluent mutator to set or replace the Content-Type header.
     * @param content_type Media type (e.g. mime::kApplicationJson).
     */
    HttpResponse& WithContentType(std::string_view content_type);

    /**
     * @brief Returns the strongly typed HttpStatus enum value.
     */
    HttpStatus Status() const { return static_cast<HttpStatus>(status_); }

    /**
     * @brief Returns the raw integer HTTP status code.
     */
    int StatusCode() const { return status_; }

    using HeaderMap = CaseInsensitiveStringMap<std::string>;

    const std::string& Body() const { return body_; }
    const HeaderMap& Headers() const { return headers_; }

    /**
     * @brief Formats the HTTP/1.1 response wire text (status line + headers + CRLF + body).
     * @param keep_alive Whether the connection should be kept alive or closed after transmission.
     * @param is_head_request When true (RFC 9110 §9.3.2), omits the payload body while preserving
     * headers.
     */
    std::string FormatWireResponse(bool keep_alive, bool is_head_request = false) const;

  private:
    uint16_t status_ = static_cast<uint16_t>(HttpStatus::kOk);
    HeaderMap headers_;
    std::string body_;
};

}  // namespace goldfish::http
