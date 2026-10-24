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
#include "goldfish/http/http_router.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace goldfish::http {

using ::testing::ElementsAre;
using ::testing::UnorderedElementsAre;

TEST(HttpRouterTest, ExactMatchRouting) {
    HttpRouter router;
    router.AddRoute(HttpMethod::kGet, "/hello",
                    [](const HttpRequest&) { return HttpResponse::String("Hello GET"); });
    router.AddRoute(HttpMethod::kPost, "/hello",
                    [](const HttpRequest&) { return HttpResponse::String("Hello POST"); });
    router.AddRoute(HttpMethod::kGet, "/status",
                    [](const HttpRequest&) { return HttpResponse::String("Status OK"); });

    auto match_get = router.Match(HttpMethod::kGet, "/hello");
    ASSERT_TRUE(match_get.has_value());
    EXPECT_EQ(match_get->kind, RouteHandler::Kind::kUnary);
    EXPECT_EQ(match_get->unary_handler(HttpRequest()).Body(), "Hello GET");

    auto match_post = router.Match(HttpMethod::kPost, "/hello");
    ASSERT_TRUE(match_post.has_value());
    EXPECT_EQ(match_post->unary_handler(HttpRequest()).Body(), "Hello POST");

    auto match_status = router.Match(HttpMethod::kGet, "/status");
    ASSERT_TRUE(match_status.has_value());
    EXPECT_EQ(match_status->unary_handler(HttpRequest()).Body(), "Status OK");

    auto match_not_found = router.Match(HttpMethod::kGet, "/unknown");
    EXPECT_FALSE(match_not_found.has_value());
}

TEST(HttpRouterTest, WildcardAndPrefixRouting) {
    HttpRouter router;
    router.AddRoute(HttpMethod::kOptions, "/*",
                    [](const HttpRequest&) { return HttpResponse::Empty(204); });
    router.AddRoute(HttpMethod::kGet, "/static/*",
                    [](const HttpRequest&) { return HttpResponse::String("Static Asset"); });
    router.AddRoute(HttpMethod::kPost, "/grpc.service.*",
                    [](const HttpRequest&) { return HttpResponse::String("gRPC wildcard"); });

    auto match_options = router.Match(HttpMethod::kOptions, "/any/sub/path");
    ASSERT_TRUE(match_options.has_value());
    EXPECT_EQ(match_options->unary_handler(HttpRequest()).Status(), HttpStatus::kNoContent);

    auto match_static = router.Match(HttpMethod::kGet, "/static/js/bundle.js");
    ASSERT_TRUE(match_static.has_value());
    EXPECT_EQ(match_static->unary_handler(HttpRequest()).Body(), "Static Asset");

    auto match_grpc = router.Match(HttpMethod::kPost, "/grpc.service.User/GetInfo");
    ASSERT_TRUE(match_grpc.has_value());
    EXPECT_EQ(match_grpc->unary_handler(HttpRequest()).Body(), "gRPC wildcard");

    // Mismatched method on wildcard
    EXPECT_FALSE(router.Match(HttpMethod::kGet, "/grpc.service.User/GetInfo").has_value());
}

TEST(HttpRouterTest, LongestPrefixTakesPrecedenceRegardlessOfRegistrationOrder) {
    HttpRouter router;
    // Register catch-all first
    router.AddRoute(HttpMethod::kGet, "/*",
                    [](const HttpRequest&) { return HttpResponse::String("CatchAll"); });
    // Register specific wildcard second
    router.AddRoute(HttpMethod::kGet, "/static/*",
                    [](const HttpRequest&) { return HttpResponse::String("StaticFiles"); });
    // Register more specific wildcard third
    router.AddRoute(HttpMethod::kGet, "/static/js/*",
                    [](const HttpRequest&) { return HttpResponse::String("JavaScriptFiles"); });

    auto match_js = router.Match(HttpMethod::kGet, "/static/js/app.js");
    ASSERT_TRUE(match_js.has_value());
    EXPECT_EQ(match_js->unary_handler(HttpRequest()).Body(), "JavaScriptFiles");

    auto match_css = router.Match(HttpMethod::kGet, "/static/css/style.css");
    ASSERT_TRUE(match_css.has_value());
    EXPECT_EQ(match_css->unary_handler(HttpRequest()).Body(), "StaticFiles");

    auto match_other = router.Match(HttpMethod::kGet, "/index.html");
    ASSERT_TRUE(match_other.has_value());
    EXPECT_EQ(match_other->unary_handler(HttpRequest()).Body(), "CatchAll");
}

TEST(HttpRouterTest, ExactMatchTakesPrecedenceOverWildcard) {
    HttpRouter router;
    router.AddRoute(HttpMethod::kGet, "/*",
                    [](const HttpRequest&) { return HttpResponse::String("Wildcard"); });
    router.AddRoute(HttpMethod::kGet, "/specific",
                    [](const HttpRequest&) { return HttpResponse::String("Specific"); });

    auto match_specific = router.Match(HttpMethod::kGet, "/specific");
    ASSERT_TRUE(match_specific.has_value());
    EXPECT_EQ(match_specific->unary_handler(HttpRequest()).Body(), "Specific");

    auto match_other = router.Match(HttpMethod::kGet, "/other");
    ASSERT_TRUE(match_other.has_value());
    EXPECT_EQ(match_other->unary_handler(HttpRequest()).Body(), "Wildcard");
}

TEST(HttpRouterTest, StreamingRouteRegistration) {
    HttpRouter router;
    bool called = false;
    router.AddStreamingRoute(
            HttpMethod::kPost, "/stream",
            [&called](const HttpRequest&, std::shared_ptr<HttpResponseWriter>) { called = true; });

    auto match = router.Match(HttpMethod::kPost, "/stream");
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->kind, RouteHandler::Kind::kStreaming);
    ASSERT_NE(match->streaming_handler, nullptr);
    match->streaming_handler(HttpRequest(), nullptr);
    EXPECT_TRUE(called);
}

TEST(HttpRouterTest, AllowedMethodsFor405) {
    HttpRouter router;
    router.AddRoute(HttpMethod::kGet, "/resource",
                    [](const HttpRequest&) { return HttpResponse::Empty(200); });
    router.AddRoute(HttpMethod::kPost, "/resource",
                    [](const HttpRequest&) { return HttpResponse::Empty(200); });
    router.AddRoute(HttpMethod::kDelete, "/resource",
                    [](const HttpRequest&) { return HttpResponse::Empty(200); });

    auto allowed = router.GetAllowedMethods("/resource");
    EXPECT_THAT(allowed, UnorderedElementsAre("DELETE", "GET", "POST"));

    auto unknown_allowed = router.GetAllowedMethods("/unknown");
    EXPECT_TRUE(unknown_allowed.empty());
}

TEST(HttpRouterTest, AnyMethodRouteMatchesAllHttpVerbs) {
    HttpRouter router;
    router.AddRoute(HttpMethod::kAny, "/catch-all",
                    [](const HttpRequest&) { return HttpResponse::String("Universal"); });

    EXPECT_TRUE(router.Match(HttpMethod::kGet, "/catch-all").has_value());
    EXPECT_TRUE(router.Match(HttpMethod::kPost, "/catch-all").has_value());
    EXPECT_TRUE(router.Match(HttpMethod::kPut, "/catch-all").has_value());
    EXPECT_TRUE(router.Match(HttpMethod::kDelete, "/catch-all").has_value());
    EXPECT_TRUE(router.Match(HttpMethod::kOptions, "/catch-all").has_value());

    auto allowed = router.GetAllowedMethods("/catch-all");
    EXPECT_THAT(allowed, ElementsAre("GET", "POST", "PUT", "DELETE", "OPTIONS", "HEAD", "PATCH"));
}

TEST(HttpRouterTest, ExactShadowsWildcardSubpath) {
    HttpRouter router;
    router.AddRoute(HttpMethod::kGet, "/static/*",
                    [](const HttpRequest&) { return HttpResponse::String("Wildcard"); });
    router.AddRoute(HttpMethod::kGet, "/static/index.html",
                    [](const HttpRequest&) { return HttpResponse::String("ExactIndex"); });

    auto match_exact = router.Match(HttpMethod::kGet, "/static/index.html");
    ASSERT_TRUE(match_exact.has_value());
    EXPECT_EQ(match_exact->unary_handler(HttpRequest()).Body(), "ExactIndex");

    auto match_other = router.Match(HttpMethod::kGet, "/static/style.css");
    ASSERT_TRUE(match_other.has_value());
    EXPECT_EQ(match_other->unary_handler(HttpRequest()).Body(), "Wildcard");
}

TEST(HttpRouterTest, ConcurrentRouteMatching) {
    HttpRouter router;
    router.AddRoute(HttpMethod::kGet, "/api/v1/user",
                    [](const HttpRequest&) { return HttpResponse::String("User"); });
    router.AddRoute(HttpMethod::kPost, "/api/v1/auth",
                    [](const HttpRequest&) { return HttpResponse::String("Auth"); });
    router.AddRoute(HttpMethod::kGet, "/static/*",
                    [](const HttpRequest&) { return HttpResponse::String("Static"); });

    constexpr int kThreads = 8;
    constexpr int kIterations = 1000;
    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&router]() {
            for (int i = 0; i < kIterations; ++i) {
                auto m1 = router.Match(HttpMethod::kGet, "/api/v1/user");
                EXPECT_TRUE(m1.has_value());
                auto m2 = router.Match(HttpMethod::kPost, "/api/v1/auth");
                EXPECT_TRUE(m2.has_value());
                auto m3 = router.Match(HttpMethod::kGet, "/static/img/pic.png");
                EXPECT_TRUE(m3.has_value());
                auto m4 = router.Match(HttpMethod::kDelete, "/nonexistent");
                EXPECT_FALSE(m4.has_value());
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }
}

TEST(HttpRouterTest, HeterogeneousStringViewLookup) {
    HttpRouter router;
    router.AddRoute(HttpMethod::kGet, "/api/v1/resource",
                    [](const HttpRequest&) { return HttpResponse::String("Resource"); });

    // Match using a string_view substring slice
    std::string full_buffer = "GET /api/v1/resource HTTP/1.1";
    std::string_view path_slice(full_buffer.data() + 4, 16);  // "/api/v1/resource"

    auto match = router.Match(HttpMethod::kGet, path_slice);
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->unary_handler(HttpRequest()).Body(), "Resource");

    auto allowed = router.GetAllowedMethods(path_slice);
    EXPECT_THAT(allowed, ElementsAre("GET"));
}

namespace {

struct LargeCapturedHandler {
    std::shared_ptr<int> copy_counter;
    std::array<uint64_t, 8> payload{};
    std::string response_body;

    LargeCapturedHandler(std::shared_ptr<int> counter, std::string body)
            : copy_counter(std::move(counter)), response_body(std::move(body)) {}

    LargeCapturedHandler(const LargeCapturedHandler& other)
            : copy_counter(other.copy_counter)
            , payload(other.payload)
            , response_body(other.response_body) {
        if (copy_counter) {
            ++(*copy_counter);
        }
    }

    LargeCapturedHandler(LargeCapturedHandler&&) noexcept = default;
    LargeCapturedHandler& operator=(const LargeCapturedHandler&) = default;
    LargeCapturedHandler& operator=(LargeCapturedHandler&&) noexcept = default;

    HttpResponse operator()(const HttpRequest&) const {
        return HttpResponse::String(response_body);
    }
};

}  // namespace

TEST(HttpRouterTest, MatchHandlerAvoidsCopyingLargeHandlerClosure) {
    HttpRouter router;
    auto copy_count = std::make_shared<int>(0);
    router.AddRoute(HttpMethod::kGet, "/api/v1/heavy",
                    LargeCapturedHandler(copy_count, "PrimaryGET"));
    router.AddRoute(HttpMethod::kPost, "/api/v1/heavy",
                    LargeCapturedHandler(copy_count, "SecondaryPOST"));
    router.AddRoute(HttpMethod::kAny, "/api/v1/fallback",
                    LargeCapturedHandler(copy_count, "FallbackANY"));
    router.AddRoute(HttpMethod::kPost, "/grpc/*", LargeCapturedHandler(copy_count, "WildcardPOST"));

    *copy_count = 0;

    const RouteHandler* get_handler = router.MatchHandler(HttpMethod::kGet, "/api/v1/heavy");
    ASSERT_NE(get_handler, nullptr);
    EXPECT_EQ(get_handler->unary_handler(HttpRequest()).Body(), "PrimaryGET");

    const RouteHandler* post_handler = router.MatchHandler(HttpMethod::kPost, "/api/v1/heavy");
    ASSERT_NE(post_handler, nullptr);
    EXPECT_EQ(post_handler->unary_handler(HttpRequest()).Body(), "SecondaryPOST");

    const RouteHandler* any_handler = router.MatchHandler(HttpMethod::kPatch, "/api/v1/fallback");
    ASSERT_NE(any_handler, nullptr);
    EXPECT_EQ(any_handler->unary_handler(HttpRequest()).Body(), "FallbackANY");

    const RouteHandler* wildcard_handler =
            router.MatchHandler(HttpMethod::kPost, "/grpc/android.emulation.control.Rtc/request");
    ASSERT_NE(wildcard_handler, nullptr);
    EXPECT_EQ(wildcard_handler->unary_handler(HttpRequest()).Body(), "WildcardPOST");

    EXPECT_EQ(router.MatchHandler(HttpMethod::kDelete, "/api/v1/heavy"), nullptr);
    EXPECT_EQ(router.MatchHandler(HttpMethod::kGet, "/missing"), nullptr);
    EXPECT_EQ(*copy_count, 0);
}

TEST(HttpRouterTest, OverwritingExactRoutesUpdatesPrimaryAndExtraHandlers) {
    HttpRouter router;
    router.AddRoute(HttpMethod::kGet, "/items",
                    [](const HttpRequest&) { return HttpResponse::String("v1-get"); });
    router.AddRoute(HttpMethod::kPost, "/items",
                    [](const HttpRequest&) { return HttpResponse::String("v1-post"); });
    router.AddRoute(HttpMethod::kPut, "/items",
                    [](const HttpRequest&) { return HttpResponse::String("v1-put"); });

    // Overwrite both primary (GET) and secondary (PUT) handlers.
    router.AddRoute(HttpMethod::kGet, "/items",
                    [](const HttpRequest&) { return HttpResponse::String("v2-get"); });
    router.AddRoute(HttpMethod::kPut, "/items",
                    [](const HttpRequest&) { return HttpResponse::String("v2-put"); });

    const RouteHandler* get_match = router.MatchHandler(HttpMethod::kGet, "/items");
    ASSERT_NE(get_match, nullptr);
    EXPECT_EQ(get_match->unary_handler(HttpRequest()).Body(), "v2-get");

    const RouteHandler* post_match = router.MatchHandler(HttpMethod::kPost, "/items");
    ASSERT_NE(post_match, nullptr);
    EXPECT_EQ(post_match->unary_handler(HttpRequest()).Body(), "v1-post");

    const RouteHandler* put_match = router.MatchHandler(HttpMethod::kPut, "/items");
    ASSERT_NE(put_match, nullptr);
    EXPECT_EQ(put_match->unary_handler(HttpRequest()).Body(), "v2-put");

    EXPECT_THAT(router.GetAllowedMethods("/items"), ElementsAre("GET", "POST", "PUT"));
}

TEST(HttpRouterTest, NonAsciiUtf8PathsRouteAndReportAllowedMethods) {
    HttpRouter router;
    router.AddRoute(HttpMethod::kGet, "/api/प्रोफ़ाइल/🚀",
                    [](const HttpRequest&) { return HttpResponse::String("ExactUtf8"); });
    router.AddRoute(HttpMethod::kOptions, "/api/प्रोफ़ाइल/*",
                    [](const HttpRequest&) { return HttpResponse::Empty(204); });

    const RouteHandler* exact = router.MatchHandler(HttpMethod::kGet, "/api/प्रोफ़ाइल/🚀");
    ASSERT_NE(exact, nullptr);
    EXPECT_EQ(exact->unary_handler(HttpRequest()).Body(), "ExactUtf8");

    const RouteHandler* prefix = router.MatchHandler(HttpMethod::kOptions, "/api/प्रोफ़ाइल/अन्य");
    ASSERT_NE(prefix, nullptr);
    EXPECT_EQ(prefix->unary_handler(HttpRequest()).Status(), HttpStatus::kNoContent);

    EXPECT_THAT(router.GetAllowedMethods("/api/प्रोफ़ाइल/🚀"), ElementsAre("GET", "OPTIONS"));
}

}  // namespace goldfish::http
