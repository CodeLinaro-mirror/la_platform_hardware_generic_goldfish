// Copyright 2026 The Android Open Source Project
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

#include "goldfish/network/http_client.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

#include "curl/curl.h"

namespace goldfish::network {
namespace {

constexpr size_t kMaxResponseBytes = 16ULL * 1024ULL * 1024ULL;  // 16 MiB cap
// The connect timeout includes DNS and the TLS handshake.
constexpr long kDefaultConnectTimeoutMs = 5000L;

struct CurlDeleter {
    void operator()(CURL* curl) const { ::curl_easy_cleanup(curl); }
};

struct CurlSlistDeleter {
    void operator()(curl_slist* slist) const { ::curl_slist_free_all(slist); }
};

using CurlSlistPtr = std::unique_ptr<curl_slist, CurlSlistDeleter>;

struct CurlWriteContext {
    std::string buffer;
    bool size_limit_exceeded = false;
};

std::string_view SanitizeUrlForLog(std::string_view url) {
    const size_t query_pos = url.find_first_of("?#");
    return (query_pos == std::string_view::npos) ? url : url.substr(0, query_pos);
}

absl::Status HttpCodeToStatus(long http_code, std::string_view message) {
    switch (http_code) {
    case 400:
        return absl::InvalidArgumentError(message);
    case 401:
        return absl::UnauthenticatedError(message);
    case 403:
        return absl::PermissionDeniedError(message);
    case 404:
        return absl::NotFoundError(message);
    case 408:
    case 504:
        return absl::DeadlineExceededError(message);
    case 409:
        return absl::AbortedError(message);
    case 416:
        return absl::OutOfRangeError(message);
    case 429:
        return absl::ResourceExhaustedError(message);
    case 499:
        return absl::CancelledError(message);
    case 501:
        return absl::UnimplementedError(message);
    case 503:
        return absl::UnavailableError(message);
    default:
        if (http_code >= 400 && http_code < 500) {
            return absl::FailedPreconditionError(message);
        }
        if (http_code >= 500 && http_code < 600) {
            return absl::UnavailableError(message);
        }
        return absl::InternalError(message);
    }
}

absl::Status CurlSlistAppend(CurlSlistPtr& slist, std::string_view header) {
    const std::string header_str(header);
    if (struct curl_slist* new_slist = ::curl_slist_append(slist.get(), header_str.c_str())) {
        slist.release();  // Consumed by curl_slist_append.
        slist.reset(new_slist);
        return absl::OkStatus();
    }
    const size_t colon = header.find(':');
    const std::string_view header_name =
            (colon == std::string_view::npos) ? header : header.substr(0, colon);
    return absl::InternalError(absl::StrCat("Failed to append HTTP header '", header_name, "'"));
}

size_t CurlWriteCallback(char* contents, size_t size, size_t nmemb, void* userp) {
    auto& ctx = *static_cast<CurlWriteContext*>(userp);
    if (size != 0 && nmemb > kMaxResponseBytes / size) {
        ctx.size_limit_exceeded = true;
        return 0;
    }
    const size_t total = size * nmemb;
    if (ctx.buffer.size() + total > kMaxResponseBytes) {
        ctx.size_limit_exceeded = true;
        return 0;
    }
    ctx.buffer.append(contents, total);
    return total;
}

// CURLOPT_XFERINFOFUNCTION callback: cancels the transfer once `cancel`, a std::function<bool()>,
// returns true. libcurl calls it at least about once per second, also while it connects or waits
// for the response.
int AbortIfCancelled(void* cancel, curl_off_t /*dltotal*/, curl_off_t /*dlnow*/,
                     curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    return (*static_cast<const std::function<bool()>*>(cancel))() ? 1 : 0;
}

void EnsureCurlInitialized() {
    static std::once_flag init_flag;
    std::call_once(init_flag, [] { ::curl_global_init(CURL_GLOBAL_ALL); });
}

class CurlHttpClient final : public HttpClient {
  public:
    CurlHttpClient() { EnsureCurlInitialized(); }

    absl::StatusOr<std::string> Get(std::string_view url, absl::Duration timeout,
                                    const std::function<bool()>& cancel) override {
        return ExecuteRequest(url, /*post_body=*/std::nullopt, /*headers=*/{}, timeout, cancel);
    }

    absl::StatusOr<std::string> Post(std::string_view url, std::string_view body,
                                     absl::Span<const std::string_view> headers,
                                     absl::Duration timeout,
                                     const std::function<bool()>& cancel) override {
        return ExecuteRequest(url, body, headers, timeout, cancel);
    }

  private:
    static absl::StatusOr<std::string> ExecuteRequest(std::string_view url,
                                                      std::optional<std::string_view> post_body,
                                                      absl::Span<const std::string_view> headers,
                                                      absl::Duration timeout,
                                                      const std::function<bool()>& cancel) {
        if (timeout <= absl::ZeroDuration()) {
            return absl::InvalidArgumentError(absl::StrCat(
                    "HTTP request timeout must be positive, got ", absl::FormatDuration(timeout)));
        }

        const std::unique_ptr<CURL, CurlDeleter> curl(::curl_easy_init());
        if (!curl) {
            return absl::InternalError("Failed to initialize libcurl handle");
        }

        CurlSlistPtr curl_headers;
        for (const std::string_view header : headers) {
            if (absl::Status s = CurlSlistAppend(curl_headers, header); !s.ok()) {
                return s;
            }
        }

        const std::string_view method = post_body.has_value() ? "POST" : "GET";
        const std::string_view sanitized_url = SanitizeUrlForLog(url);
        const std::string url_str(url);
        CurlWriteContext write_ctx;
        char error_buffer[CURL_ERROR_SIZE] = {0};

        const int64_t clamped_timeout_ms = std::clamp<int64_t>(absl::ToInt64Milliseconds(timeout),
                                                               1, std::numeric_limits<long>::max());
        const long timeout_ms = static_cast<long>(clamped_timeout_ms);
        const long connect_timeout_ms = std::min<long>(timeout_ms, kDefaultConnectTimeoutMs);

        ::curl_easy_setopt(curl.get(), CURLOPT_URL, url_str.c_str());
        ::curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
        ::curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
        ::curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
        ::curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 10L);
        ::curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms);
        ::curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, timeout_ms);
        ::curl_easy_setopt(curl.get(), CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
        ::curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, error_buffer);
        ::curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, &CurlWriteCallback);
        ::curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &write_ctx);
        if (cancel) {
            ::curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, &AbortIfCancelled);
            ::curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &cancel);
            ::curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
        }
        if (curl_headers) {
            ::curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, curl_headers.get());
        }
        if (post_body.has_value()) {
            ::curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
            ::curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS,
                               post_body->data() != nullptr ? post_body->data() : "");
            ::curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                               static_cast<curl_off_t>(post_body->size()));
        }
        if (VLOG_IS_ON(1)) {
            ::curl_easy_setopt(curl.get(), CURLOPT_VERBOSE, 1L);
        }

        const CURLcode res = ::curl_easy_perform(curl.get());
        if (res != CURLE_OK) {
            if (write_ctx.size_limit_exceeded) {
                return absl::ResourceExhaustedError(absl::StrCat(
                        "HTTP ", method, " ", sanitized_url, " response exceeded maximum size of ",
                        kMaxResponseBytes, " bytes"));
            }
            const std::string_view detail = error_buffer[0] != '\0'
                                                    ? std::string_view(error_buffer)
                                                    : std::string_view(::curl_easy_strerror(res));
            if (res == CURLE_ABORTED_BY_CALLBACK) {
                return absl::CancelledError(absl::StrCat("HTTP ", method, " ", sanitized_url,
                                                         " was cancelled: ", detail));
            }
            if (res == CURLE_OPERATION_TIMEDOUT) {
                return absl::DeadlineExceededError(
                        absl::StrCat("HTTP ", method, " ", sanitized_url, " timed out after ",
                                     absl::FormatDuration(timeout), ": ", detail));
            }
            if (res == CURLE_URL_MALFORMAT || res == CURLE_UNSUPPORTED_PROTOCOL) {
                return absl::InvalidArgumentError(absl::StrCat("Invalid URL for HTTP ", method,
                                                               " '", sanitized_url, "': ", detail));
            }
            return absl::UnavailableError(
                    absl::StrCat("HTTP ", method, " ", sanitized_url, " failed: ", detail));
        }

        long http_code = 0;
        const CURLcode info_res =
                ::curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
        if (info_res != CURLE_OK) {
            return absl::InternalError(absl::StrCat("Failed to read HTTP response status code for ",
                                                    method, " ", sanitized_url, ": ",
                                                    ::curl_easy_strerror(info_res)));
        }

        const bool is_file_url = absl::StartsWithIgnoreCase(url, "file://");
        if ((http_code < 200 || http_code >= 300) && !(is_file_url && http_code == 0)) {
            return HttpCodeToStatus(http_code,
                                    absl::StrCat("HTTP ", method, " ", sanitized_url,
                                                 " returned unexpected status code: ", http_code));
        }

        return std::move(write_ctx.buffer);
    }
};

}  // namespace

std::unique_ptr<HttpClient> CreateCurlHttpClient() {
    return std::make_unique<CurlHttpClient>();
}

}  // namespace goldfish::network
