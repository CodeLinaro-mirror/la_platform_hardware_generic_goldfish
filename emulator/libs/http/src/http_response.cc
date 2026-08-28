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
#include "goldfish/http/http_response.h"

#include <string>
#include <string_view>
#include <utility>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace goldfish::http {

std::string_view HttpStatusToReason(HttpStatus status) {
    return HttpStatusToReason(static_cast<int>(status));
}

std::string_view HttpStatusToReason(int status) {
    switch (static_cast<HttpStatus>(status)) {
    // 1xx
    case HttpStatus::kContinue:
        return "Continue";
    case HttpStatus::kSwitchingProtocols:
        return "Switching Protocols";

    // 2xx
    case HttpStatus::kOk:
        return "OK";
    case HttpStatus::kCreated:
        return "Created";
    case HttpStatus::kAccepted:
        return "Accepted";
    case HttpStatus::kNonAuthoritativeInformation:
        return "Non-Authoritative Information";
    case HttpStatus::kNoContent:
        return "No Content";
    case HttpStatus::kResetContent:
        return "Reset Content";
    case HttpStatus::kPartialContent:
        return "Partial Content";

    // 3xx
    case HttpStatus::kMultipleChoices:
        return "Multiple Choices";
    case HttpStatus::kMovedPermanently:
        return "Moved Permanently";
    case HttpStatus::kFound:
        return "Found";
    case HttpStatus::kSeeOther:
        return "See Other";
    case HttpStatus::kNotModified:
        return "Not Modified";
    case HttpStatus::kUseProxy:
        return "Use Proxy";
    case HttpStatus::kTemporaryRedirect:
        return "Temporary Redirect";
    case HttpStatus::kPermanentRedirect:
        return "Permanent Redirect";

    // 4xx
    case HttpStatus::kBadRequest:
        return "Bad Request";
    case HttpStatus::kUnauthorized:
        return "Unauthorized";
    case HttpStatus::kPaymentRequired:
        return "Payment Required";
    case HttpStatus::kForbidden:
        return "Forbidden";
    case HttpStatus::kNotFound:
        return "Not Found";
    case HttpStatus::kMethodNotAllowed:
        return "Method Not Allowed";
    case HttpStatus::kNotAcceptable:
        return "Not Acceptable";
    case HttpStatus::kProxyAuthenticationRequired:
        return "Proxy Authentication Required";
    case HttpStatus::kRequestTimeout:
        return "Request Timeout";
    case HttpStatus::kConflict:
        return "Conflict";
    case HttpStatus::kGone:
        return "Gone";
    case HttpStatus::kLengthRequired:
        return "Length Required";
    case HttpStatus::kPreconditionFailed:
        return "Precondition Failed";
    case HttpStatus::kPayloadTooLarge:
        return "Payload Too Large";
    case HttpStatus::kUriTooLong:
        return "URI Too Long";
    case HttpStatus::kUnsupportedMediaType:
        return "Unsupported Media Type";
    case HttpStatus::kRangeNotSatisfiable:
        return "Range Not Satisfiable";
    case HttpStatus::kExpectationFailed:
        return "Expectation Failed";
    case HttpStatus::kImATeapot:
        return "I'm a teapot";
    case HttpStatus::kMisdirectedRequest:
        return "Misdirected Request";
    case HttpStatus::kUnprocessableEntity:
        return "Unprocessable Entity";
    case HttpStatus::kLocked:
        return "Locked";
    case HttpStatus::kFailedDependency:
        return "Failed Dependency";
    case HttpStatus::kTooEarly:
        return "Too Early";
    case HttpStatus::kUpgradeRequired:
        return "Upgrade Required";
    case HttpStatus::kPreconditionRequired:
        return "Precondition Required";
    case HttpStatus::kTooManyRequests:
        return "Too Many Requests";
    case HttpStatus::kRequestHeaderFieldsTooLarge:
        return "Request Header Fields Too Large";
    case HttpStatus::kUnavailableForLegalReasons:
        return "Unavailable For Legal Reasons";

    // 5xx
    case HttpStatus::kInternalServerError:
        return "Internal Server Error";
    case HttpStatus::kNotImplemented:
        return "Not Implemented";
    case HttpStatus::kBadGateway:
        return "Bad Gateway";
    case HttpStatus::kServiceUnavailable:
        return "Service Unavailable";
    case HttpStatus::kGatewayTimeout:
        return "Gateway Timeout";
    case HttpStatus::kHttpVersionNotSupported:
        return "HTTP Version Not Supported";
    case HttpStatus::kVariantAlsoNegotiates:
        return "Variant Also Negotiates";
    case HttpStatus::kInsufficientStorage:
        return "Insufficient Storage";
    case HttpStatus::kLoopDetected:
        return "Loop Detected";
    case HttpStatus::kNotExtended:
        return "Not Extended";
    case HttpStatus::kNetworkAuthenticationRequired:
        return "Network Authentication Required";

    default:
        return "Unknown Status";
    }
}

HttpResponse HttpResponse::String(std::string body, HttpStatus status,
                                  std::string_view content_type) {
    return String(std::move(body), static_cast<int>(status), content_type);
}

HttpResponse HttpResponse::String(std::string body, int status, std::string_view content_type) {
    HttpResponse resp;
    resp.status_ = static_cast<uint16_t>(status);
    resp.body_ = std::move(body);
    if (!content_type.empty()) {
        resp.headers_.insert_or_assign(std::string(headers::kContentType),
                                       std::string(content_type));
    }
    return resp;
}

HttpResponse HttpResponse::Empty(HttpStatus status) {
    return Empty(static_cast<int>(status));
}

HttpResponse HttpResponse::Empty(int status) {
    HttpResponse resp;
    resp.status_ = static_cast<uint16_t>(status);
    return resp;
}

HttpResponse& HttpResponse::WithStatus(HttpStatus status) {
    status_ = static_cast<uint16_t>(status);
    return *this;
}

HttpResponse& HttpResponse::WithStatus(int status) {
    status_ = static_cast<uint16_t>(status);
    return *this;
}

HttpResponse& HttpResponse::WithHeader(std::string_view name, std::string_view value) {
    headers_.insert_or_assign(std::string(name), std::string(value));
    return *this;
}

HttpResponse& HttpResponse::WithContentType(std::string_view content_type) {
    headers_.insert_or_assign(std::string(headers::kContentType), std::string(content_type));
    return *this;
}

std::string HttpResponse::FormatWireResponse(bool keep_alive, bool is_head_request) const {
    std::string response;
    response.reserve(body_.size() + 256);
    absl::StrAppend(&response, protocol::kHttp11, protocol::kSpace, status_, protocol::kSpace,
                    HttpStatusToReason(status_), protocol::kCrlf);

    bool has_content_length = false;
    bool has_connection = false;

    for (const auto& [name, value] : headers_) {
        absl::StrAppend(&response, name, protocol::kColonSpace, value, protocol::kCrlf);
        if (absl::EqualsIgnoreCase(name, headers::kContentLength)) {
            has_content_length = true;
        }
        if (absl::EqualsIgnoreCase(name, headers::kConnection)) {
            has_connection = true;
        }
    }

    bool forbids_content_length = (status_ >= 100 && status_ < 200) || status_ == 204;
    if (!has_content_length && !forbids_content_length) {
        absl::StrAppend(&response, headers::kContentLength, protocol::kColonSpace, body_.size(),
                        protocol::kCrlf);
    }
    if (!has_connection) {
        absl::StrAppend(&response, headers::kConnection, protocol::kColonSpace,
                        keep_alive ? protocol::kKeepAlive : protocol::kClose, protocol::kCrlf);
    }

    absl::StrAppend(&response, protocol::kCrlf);
    if (!is_head_request && status_ != 204 && status_ != 304 && (status_ < 100 || status_ >= 200)) {
        absl::StrAppend(&response, body_);
    }
    return response;
}

}  // namespace goldfish::http
