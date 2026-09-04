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

#include <cstdint>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"

namespace goldfish::http {

// Heterogeneous Lookup Helpers
// See: https://abseil.io/tips/144 (Heterogeneous Lookup in Associative Containers)
//
// By providing `using is_transparent = void;` on custom hash and equality functors,
// absl::flat_hash_map can perform lookups with `std::string_view` keys directly without
// creating temporary `std::string` heap allocations.

/**
 * @brief Transparent hash functor enabling zero-allocation heterogeneous string_view lookup
 * in flat hash containers.
 *
 * @see https://abseil.io/tips/144 Heterogeneous Lookup in Associative Containers
 */
struct TransparentStringHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const noexcept {
        return absl::Hash<std::string_view>{}(sv);
    }
};

/**
 * @brief Transparent equality functor enabling zero-allocation heterogeneous string_view comparison
 * in flat hash containers.
 *
 * @see https://abseil.io/tips/144 Heterogeneous Lookup in Associative Containers
 */
struct TransparentStringEq {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
};

/**
 * @brief Alias for absl::flat_hash_map with transparent zero-allocation lookup on std::string_view.
 *
 * @see https://abseil.io/tips/144 Heterogeneous Lookup in Associative Containers
 */
template <typename Value>
using TransparentStringMap =
        absl::flat_hash_map<std::string, Value, TransparentStringHash, TransparentStringEq>;

/**
 * @brief Transparent case-insensitive hash functor enabling zero-allocation lookup
 * in flat hash containers.
 *
 * Uses absl::Hash with a transparent wrapper to perform case-insensitive hashing
 * without temporary string copies.
 *
 * @see https://abseil.io/tips/144 Heterogeneous Lookup in Associative Containers
 */
struct CaseInsensitiveStringHash {
    using is_transparent = void;

    size_t operator()(std::string_view sv) const noexcept {
        return absl::Hash<Wrapper>{}(Wrapper{sv});
    }

  private:
    struct Wrapper {
        std::string_view value;

        template <typename H>
        friend H AbslHashValue(H h, Wrapper str) {
            for (const char c : str.value) {
                h = H::combine(std::move(h), static_cast<unsigned char>(absl::ascii_tolower(c)));
            }
            return h;
        }
    };
};

/**
 * @brief Transparent case-insensitive equality functor enabling zero-allocation lookup
 * in flat hash containers.
 *
 * @see https://abseil.io/tips/144 Heterogeneous Lookup in Associative Containers
 */
struct CaseInsensitiveStringEq {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept {
        return absl::EqualsIgnoreCase(a, b);
    }
};

/**
 * @brief Alias for absl::flat_hash_map with case-insensitive transparent string_view lookup.
 *
 * @see https://abseil.io/tips/144 Heterogeneous Lookup in Associative Containers
 */
template <typename Value>
using CaseInsensitiveStringMap =
        absl::flat_hash_map<std::string, Value, CaseInsensitiveStringHash, CaseInsensitiveStringEq>;

/**
 * @brief Strongly typed enumeration of standard HTTP/1.1 status codes (RFC 9110).
 *
 * @section usage_example Usage Example
 * @code
 * #include "goldfish/http/http_headers.h"
 * #include "goldfish/http/http_response.h"
 *
 * return HttpResponse::String("Created", HttpStatus::kCreated);
 * return HttpResponse::Empty(HttpStatus::kNoContent);
 * return HttpResponse::String("Bad Request", HttpStatus::kBadRequest);
 * @endcode
 */
enum class HttpStatus : uint16_t {
    // 1xx Informational
    kContinue = 100,
    kSwitchingProtocols = 101,

    // 2xx Success
    kOk = 200,
    kCreated = 201,
    kAccepted = 202,
    kNonAuthoritativeInformation = 203,
    kNoContent = 204,
    kResetContent = 205,
    kPartialContent = 206,

    // 3xx Redirection
    kMultipleChoices = 300,
    kMovedPermanently = 301,
    kFound = 302,
    kSeeOther = 303,
    kNotModified = 304,
    kUseProxy = 305,
    kTemporaryRedirect = 307,
    kPermanentRedirect = 308,

    // 4xx Client Error
    kBadRequest = 400,
    kUnauthorized = 401,
    kPaymentRequired = 402,
    kForbidden = 403,
    kNotFound = 404,
    kMethodNotAllowed = 405,
    kNotAcceptable = 406,
    kProxyAuthenticationRequired = 407,
    kRequestTimeout = 408,
    kConflict = 409,
    kGone = 410,
    kLengthRequired = 411,
    kPreconditionFailed = 412,
    kPayloadTooLarge = 413,
    kUriTooLong = 414,
    kUnsupportedMediaType = 415,
    kRangeNotSatisfiable = 416,
    kExpectationFailed = 417,
    kImATeapot = 418,
    kMisdirectedRequest = 421,
    kUnprocessableEntity = 422,
    kLocked = 423,
    kFailedDependency = 424,
    kTooEarly = 425,
    kUpgradeRequired = 426,
    kPreconditionRequired = 428,
    kTooManyRequests = 429,
    kRequestHeaderFieldsTooLarge = 431,
    kUnavailableForLegalReasons = 451,

    // 5xx Server Error
    kInternalServerError = 500,
    kNotImplemented = 501,
    kBadGateway = 502,
    kServiceUnavailable = 503,
    kGatewayTimeout = 504,
    kHttpVersionNotSupported = 505,
    kVariantAlsoNegotiates = 506,
    kInsufficientStorage = 507,
    kLoopDetected = 508,
    kNotExtended = 510,
    kNetworkAuthenticationRequired = 511,
};

/**
 * @brief Converts an HttpStatus enum value into its standard RFC reason phrase (e.g.
 * HttpStatus::kOk -> "OK").
 * @param status Strongly typed HttpStatus enum.
 * @return Reason phrase string view.
 */
std::string_view HttpStatusToReason(HttpStatus status);

/**
 * @brief Converts an integer HTTP status code into its standard RFC reason phrase (e.g. 200 ->
 * "OK").
 * @param status Integer HTTP status code.
 * @return Reason phrase string view.
 */
std::string_view HttpStatusToReason(int status);

/**
 * @brief Well-known standard HTTP header names (lowercase for zero-allocation lookup).
 *
 * @section usage_example Usage Example
 * @code
 * #include "goldfish/http/http_headers.h"
 * #include "goldfish/http/web_server.h"
 *
 * auto auth = req.GetHeader(headers::kAuthorization);
 * auto resp = HttpResponse::String("Hello")
 *                 .WithHeader(headers::kContentType, mime::kTextPlain)
 *                 .WithHeader(headers::kAccessControlAllowOrigin, "*");
 * @endcode
 */
namespace headers {

// Standard HTTP Headers (RFC 7230, RFC 7231, RFC 9110)
inline constexpr std::string_view kAccept = "accept";
inline constexpr std::string_view kAcceptEncoding = "accept-encoding";
inline constexpr std::string_view kAllow = "allow";
inline constexpr std::string_view kAuthorization = "authorization";
inline constexpr std::string_view kCacheControl = "cache-control";
inline constexpr std::string_view kConnection = "connection";
inline constexpr std::string_view kContentEncoding = "content-encoding";
inline constexpr std::string_view kContentLength = "content-length";
inline constexpr std::string_view kContentType = "content-type";
inline constexpr std::string_view kDate = "date";
inline constexpr std::string_view kHost = "host";
inline constexpr std::string_view kLocation = "location";
inline constexpr std::string_view kOrigin = "origin";
inline constexpr std::string_view kProxyAuthorization = "proxy-authorization";
inline constexpr std::string_view kServer = "server";
inline constexpr std::string_view kTransferEncoding = "transfer-encoding";
inline constexpr std::string_view kUserAgent = "user-agent";
inline constexpr std::string_view kVary = "vary";

// CORS (Cross-Origin Resource Sharing) Headers
inline constexpr std::string_view kAccessControlAllowOrigin = "access-control-allow-origin";
inline constexpr std::string_view kAccessControlAllowMethods = "access-control-allow-methods";
inline constexpr std::string_view kAccessControlAllowHeaders = "access-control-allow-headers";
inline constexpr std::string_view kAccessControlAllowCredentials =
        "access-control-allow-credentials";
inline constexpr std::string_view kAccessControlExposeHeaders = "access-control-expose-headers";
inline constexpr std::string_view kAccessControlMaxAge = "access-control-max-age";
inline constexpr std::string_view kAccessControlRequestHeaders = "access-control-request-headers";
inline constexpr std::string_view kAccessControlRequestMethod = "access-control-request-method";

// gRPC / gRPC-Web Protocol Headers
inline constexpr std::string_view kGrpcStatus = "grpc-status";
inline constexpr std::string_view kGrpcMessage = "grpc-message";
inline constexpr std::string_view kGrpcStatusDetailsBin = "grpc-status-details-bin";
inline constexpr std::string_view kGrpcTimeout = "grpc-timeout";
inline constexpr std::string_view kGrpcAcceptEncoding = "grpc-accept-encoding";
inline constexpr std::string_view kGrpcEncoding = "grpc-encoding";
inline constexpr std::string_view kXGrpcWeb = "x-grpc-web";
inline constexpr std::string_view kXUserAgent = "x-user-agent";

}  // namespace headers

/**
 * @brief Well-known standard HTTP method verb constants.
 */
namespace methods {

inline constexpr std::string_view kGet = "GET";
inline constexpr std::string_view kPost = "POST";
inline constexpr std::string_view kPut = "PUT";
inline constexpr std::string_view kDelete = "DELETE";
inline constexpr std::string_view kOptions = "OPTIONS";
inline constexpr std::string_view kHead = "HEAD";
inline constexpr std::string_view kPatch = "PATCH";
inline constexpr std::string_view kAny = "*";

}  // namespace methods

/**
 * @brief Well-known MIME media types for Content-Type headers.
 */
namespace mime {

inline constexpr std::string_view kApplicationJson = "application/json";
inline constexpr std::string_view kApplicationOctetStream = "application/octet-stream";
inline constexpr std::string_view kApplicationGrpcWeb = "application/grpc-web";
inline constexpr std::string_view kApplicationGrpcWebProto = "application/grpc-web+proto";
inline constexpr std::string_view kApplicationGrpcWebText = "application/grpc-web-text";
inline constexpr std::string_view kApplicationGrpcWebTextProto = "application/grpc-web-text+proto";
inline constexpr std::string_view kApplicationJavascript = "application/javascript";
inline constexpr std::string_view kTextPlain = "text/plain";
inline constexpr std::string_view kTextHtml = "text/html";
inline constexpr std::string_view kTextCss = "text/css";

}  // namespace mime

/**
 * @brief Well-known HTTP protocol framing and header value constants.
 */
namespace protocol {

inline constexpr std::string_view kHttp11 = "HTTP/1.1";
inline constexpr std::string_view kCrlf = "\r\n";
inline constexpr std::string_view kDoubleCrlf = "\r\n\r\n";
inline constexpr std::string_view kColonSpace = ": ";
inline constexpr std::string_view kColon = ":";
inline constexpr std::string_view kSpace = " ";
inline constexpr std::string_view kKeepAlive = "keep-alive";
inline constexpr std::string_view kClose = "close";
inline constexpr std::string_view kTrue = "true";

}  // namespace protocol

}  // namespace goldfish::http
