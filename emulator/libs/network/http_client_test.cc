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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"

#include "goldfish/http/web_server.h"

namespace goldfish::network {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::goldfish::http::CreateWebServer;
using ::goldfish::http::HttpMethod;
using ::goldfish::http::HttpRequest;
using ::goldfish::http::HttpResponse;
using ::goldfish::http::HttpResponseWriter;
using ::goldfish::http::HttpStatus;
using ::goldfish::http::WebServer;
using ::testing::HasSubstr;
using ::testing::Not;

TEST(HttpClientTest, GetReturnsResponseBodyOnHttp200) {
    int request_count = 0;
    HttpMethod last_method = HttpMethod::kAny;
    std::string last_path;

    WebServer server{CreateWebServer(0).BindAddress("127.0.0.1")};
    server.OnGet("/*", [&](const HttpRequest& req) {
        ++request_count;
        last_method = req.Method();
        last_path = std::string(req.Path());
        return HttpResponse::String("hello from server", HttpStatus::kOk);
    });
    ASSERT_THAT(server.Start(), IsOk());

    const std::string base_url =
            absl::StrCat("http://127.0.0.1:", GetPortFromEndpoint(server.GetEndpoint()));
    auto client = CreateCurlHttpClient();
    auto resp = client->Get(absl::StrCat(base_url, "/manifest.xml?tool=emulator"), absl::Seconds(5),
                            /*cancel=*/{});
    server.Stop();

    EXPECT_THAT(resp, IsOkAndHolds("hello from server"));
    EXPECT_EQ(request_count, 1);
    EXPECT_EQ(last_method, HttpMethod::kGet);
    EXPECT_EQ(last_path, "/manifest.xml?tool=emulator");
}

TEST(HttpClientTest, PostSendsBodyAndHeaders) {
    int request_count = 0;
    HttpMethod last_method = HttpMethod::kAny;
    std::string last_path;
    std::string last_custom_header;
    std::string last_body;

    WebServer server{CreateWebServer(0).BindAddress("127.0.0.1")};
    server.OnPost("/*", [&](const HttpRequest& req) {
        ++request_count;
        last_method = req.Method();
        last_path = std::string(req.Path());
        last_custom_header = std::string(req.GetHeader("X-Test-Header"));
        last_body = std::string(req.Body());
        return HttpResponse::String("post-ack", HttpStatus::kOk);
    });
    ASSERT_THAT(server.Start(), IsOk());

    const std::string base_url =
            absl::StrCat("http://127.0.0.1:", GetPortFromEndpoint(server.GetEndpoint()));
    auto client = CreateCurlHttpClient();
    constexpr std::string_view kHeaders[] = {
        "X-Test-Header: custom-value",
        "Content-Type: text/plain",
    };
    auto resp = client->Post(absl::StrCat(base_url, "/upload"), "payload-bytes", kHeaders,
                             absl::Seconds(5), /*cancel=*/{});
    server.Stop();

    EXPECT_THAT(resp, IsOkAndHolds("post-ack"));
    EXPECT_EQ(request_count, 1);
    EXPECT_EQ(last_method, HttpMethod::kPost);
    EXPECT_EQ(last_path, "/upload");
    EXPECT_EQ(last_custom_header, "custom-value");
    EXPECT_EQ(last_body, "payload-bytes");
}

TEST(HttpClientTest, PostHandlesDefaultConstructedEmptyBodyAnd2xxStatus) {
    int request_count = 0;
    std::string last_path;
    std::string last_body;

    WebServer server{CreateWebServer(0).BindAddress("127.0.0.1")};
    server.OnPost("/*", [&](const HttpRequest& req) {
        ++request_count;
        last_path = std::string(req.Path());
        last_body = std::string(req.Body());
        return HttpResponse::Empty(HttpStatus::kNoContent);
    });
    ASSERT_THAT(server.Start(), IsOk());

    const std::string base_url =
            absl::StrCat("http://127.0.0.1:", GetPortFromEndpoint(server.GetEndpoint()));
    auto client = CreateCurlHttpClient();
    auto resp = client->Post(absl::StrCat(base_url, "/empty"), std::string_view(), {},
                             absl::Seconds(5), /*cancel=*/{});
    server.Stop();

    EXPECT_THAT(resp, IsOkAndHolds(""));
    EXPECT_EQ(request_count, 1);
    EXPECT_EQ(last_path, "/empty");
    EXPECT_EQ(last_body, "");
}

TEST(HttpClientTest, Non2xxStatusMapsToCanonicalAbslStatusAndStripsQuery) {
    WebServer server{CreateWebServer(0).BindAddress("127.0.0.1")};
    server.OnGet("/missing", [](const HttpRequest&) {
        return HttpResponse::String("not found", HttpStatus::kNotFound);
    });
    server.OnGet("/forbidden", [](const HttpRequest&) {
        return HttpResponse::String("denied", HttpStatus::kForbidden);
    });
    server.OnGet("/error", [](const HttpRequest&) {
        return HttpResponse::String("server error", HttpStatus::kInternalServerError);
    });
    ASSERT_THAT(server.Start(), IsOk());

    const std::string base_url =
            absl::StrCat("http://127.0.0.1:", GetPortFromEndpoint(server.GetEndpoint()));
    auto client = CreateCurlHttpClient();

    auto resp_404 = client->Get(absl::StrCat(base_url, "/missing?token=secret123"),
                                absl::Seconds(5), /*cancel=*/{});
    EXPECT_THAT(resp_404, StatusIs(absl::StatusCode::kNotFound));
    EXPECT_THAT(resp_404.status().message(), HasSubstr("/missing"));
    EXPECT_THAT(resp_404.status().message(), Not(HasSubstr("secret123")));

    auto resp_403 =
            client->Get(absl::StrCat(base_url, "/forbidden"), absl::Seconds(5), /*cancel=*/{});
    EXPECT_THAT(resp_403, StatusIs(absl::StatusCode::kPermissionDenied));

    auto resp_500 = client->Get(absl::StrCat(base_url, "/error"), absl::Seconds(5), /*cancel=*/{});
    EXPECT_THAT(resp_500, StatusIs(absl::StatusCode::kUnavailable));

    server.Stop();
}

TEST(HttpClientTest, NonPositiveTimeoutReturnsInvalidArgument) {
    auto client = CreateCurlHttpClient();
    EXPECT_THAT(client->Get("http://127.0.0.1:1", absl::ZeroDuration(), /*cancel=*/{}),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(client->Get("http://127.0.0.1:1", absl::Seconds(-1), /*cancel=*/{}),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(HttpClientTest, CancelsWhenCancelCallbackReturnsTrue) {
    WebServer server{CreateWebServer(0).BindAddress("127.0.0.1")};
    server.OnPost("/*",
                  [](const HttpRequest&) { return HttpResponse::String("ok", HttpStatus::kOk); });
    ASSERT_THAT(server.Start(), IsOk());

    const std::string base_url =
            absl::StrCat("http://127.0.0.1:", GetPortFromEndpoint(server.GetEndpoint()));
    auto client = CreateCurlHttpClient();
    auto resp = client->Post(absl::StrCat(base_url, "/upload"), "payload", {}, absl::Seconds(5),
                             [] { return true; });
    server.Stop();

    EXPECT_THAT(resp, StatusIs(absl::StatusCode::kCancelled));
}

TEST(HttpClientTest, TimeoutReturnsDeadlineExceeded) {
    WebServer server{CreateWebServer(0).BindAddress("127.0.0.1")};
    server.OnStream(HttpMethod::kGet, "/stall",
                    [](const HttpRequest&, std::shared_ptr<HttpResponseWriter> /*writer*/) {
                        // Leave stream open without responding to trigger request timeout.
                    });
    ASSERT_THAT(server.Start(), IsOk());

    const std::string base_url =
            absl::StrCat("http://127.0.0.1:", GetPortFromEndpoint(server.GetEndpoint()));
    auto client = CreateCurlHttpClient();
    auto resp = client->Get(absl::StrCat(base_url, "/stall"), absl::Milliseconds(100),
                            /*cancel=*/{});
    server.Stop();

    EXPECT_THAT(resp, StatusIs(absl::StatusCode::kDeadlineExceeded));
}

TEST(HttpClientTest, FollowsHttpRedirectAndRejectsNonHttpRedirectProtocol) {
    WebServer server{CreateWebServer(0).BindAddress("127.0.0.1")};
    std::string base_url;
    server.OnGet("/target", [](const HttpRequest&) {
        return HttpResponse::String("redirected-ok", HttpStatus::kOk);
    });
    server.OnGet("/redirect-http", [&](const HttpRequest&) {
        return HttpResponse::Empty(HttpStatus::kFound)
                .WithHeader("Location", absl::StrCat(base_url, "/target"));
    });
    server.OnGet("/redirect-file", [](const HttpRequest&) {
        return HttpResponse::Empty(HttpStatus::kFound).WithHeader("Location", "file:///etc/passwd");
    });
    ASSERT_THAT(server.Start(), IsOk());
    base_url = absl::StrCat("http://127.0.0.1:", GetPortFromEndpoint(server.GetEndpoint()));

    auto client = CreateCurlHttpClient();
    EXPECT_THAT(client->Get(absl::StrCat(base_url, "/redirect-http"), absl::Seconds(5),
                            /*cancel=*/{}),
                IsOkAndHolds("redirected-ok"));

    EXPECT_THAT(client->Get(absl::StrCat(base_url, "/redirect-file"), absl::Seconds(5),
                            /*cancel=*/{}),
                StatusIs(absl::StatusCode::kInvalidArgument));

    EXPECT_THAT(client->Get("://malformed-url", absl::Seconds(5), /*cancel=*/{}),
                StatusIs(absl::StatusCode::kInvalidArgument));

    server.Stop();
}

TEST(HttpClientTest, ResponseExceedingMaxSizeReturnsResourceExhausted) {
    WebServer server{CreateWebServer(0).BindAddress("127.0.0.1")};
    server.OnGet("/oversized", [](const HttpRequest&) {
        return HttpResponse::String(std::string((16 * 1024 * 1024) + 1, 'x'), HttpStatus::kOk);
    });
    ASSERT_THAT(server.Start(), IsOk());

    const std::string base_url =
            absl::StrCat("http://127.0.0.1:", GetPortFromEndpoint(server.GetEndpoint()));
    auto client = CreateCurlHttpClient();
    auto resp = client->Get(absl::StrCat(base_url, "/oversized"), absl::Seconds(5), /*cancel=*/{});
    server.Stop();

    EXPECT_THAT(resp.status(), StatusIs(absl::StatusCode::kResourceExhausted));
}

}  // namespace
}  // namespace goldfish::network
