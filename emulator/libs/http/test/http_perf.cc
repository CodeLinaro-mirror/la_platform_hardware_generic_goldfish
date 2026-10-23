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

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/hash/hash.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "benchmark/benchmark.h"

#include "goldfish/http/http_headers.h"
#include "goldfish/http/http_request.h"
#include "goldfish/http/http_router.h"

namespace goldfish::http {
namespace {

struct LegacyCaseInsensitiveStringHash {
    using is_transparent = void;

    size_t operator()(std::string_view sv) const noexcept {
        return absl::Hash<Wrapper>{}(Wrapper{sv});
    }

  private:
    struct Wrapper {
        std::string_view sv;
        template <typename H>
        friend H AbslHashValue(H h, Wrapper w) {
            for (char c : w.sv) {
                h = H::combine(std::move(h), static_cast<unsigned char>(absl::ascii_tolower(c)));
            }
            return H::combine(std::move(h), w.sv.size());
        }
    };
};

struct LegacyCaseInsensitiveStringEq {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept {
        return absl::EqualsIgnoreCase(a, b);
    }
};

using LegacyHeaderMap =
        absl::flat_hash_map<std::string, std::string, LegacyCaseInsensitiveStringHash,
                            LegacyCaseInsensitiveStringEq>;

constexpr std::array<std::pair<std::string_view, std::string_view>, 8> kSampleHeaders = {{
    {"host", "127.0.0.1:8554"},
    {"content-type", "application/grpc-web+proto"},
    {"content-length", "128"},
    {"connection", "keep-alive"},
    {"origin", "http://localhost:8080"},
    {"authorization", "Bearer eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9"},
    {"x-grpc-web", "1"},
    {"access-control-request-headers", "content-type,x-grpc-web,authorization"},
}};

constexpr std::array<std::string_view, 8> kLookupKeys = {{
    "Host",
    "Content-Type",
    "Content-Length",
    "Connection",
    "Origin",
    "Authorization",
    "X-Grpc-Web",
    "Access-Control-Request-Headers",
}};

constexpr std::array<std::string_view, 4> kCorsLookupKeys = {{
    "Access-Control-Allow-Origin",
    "Access-Control-Allow-Methods",
    "Access-Control-Allow-Headers",
    "Access-Control-Request-Headers",
}};

void BmCaseInsensitiveHeaderLookupBaseline(benchmark::State& state) {
    LegacyHeaderMap headers;
    for (const auto& [k, v] : kSampleHeaders) {
        headers.emplace(k, v);
    }

    size_t idx = 0;
    for (auto _ : state) {
        auto it = headers.find(kLookupKeys[idx & 7]);
        benchmark::DoNotOptimize(it);
        ++idx;
    }
}
BENCHMARK(BmCaseInsensitiveHeaderLookupBaseline);

void BmCaseInsensitiveHeaderLookup(benchmark::State& state) {
    HttpRequest::HeaderMap headers;
    for (const auto& [k, v] : kSampleHeaders) {
        headers.emplace(k, v);
    }

    size_t idx = 0;
    for (auto _ : state) {
        auto it = headers.find(kLookupKeys[idx & 7]);
        benchmark::DoNotOptimize(it);
        ++idx;
    }
}
BENCHMARK(BmCaseInsensitiveHeaderLookup);

void BmCaseInsensitiveCorsHeaderLookupBaseline(benchmark::State& state) {
    LegacyHeaderMap headers = {
        {"access-control-allow-origin", "*"},
        {"access-control-allow-methods", "GET, POST, OPTIONS"},
        {"access-control-allow-headers", "content-type,x-grpc-web,authorization"},
        {"access-control-request-headers", "content-type,x-grpc-web"},
    };

    size_t idx = 0;
    for (auto _ : state) {
        auto it = headers.find(kCorsLookupKeys[idx & 3]);
        benchmark::DoNotOptimize(it);
        ++idx;
    }
}
BENCHMARK(BmCaseInsensitiveCorsHeaderLookupBaseline);

void BmCaseInsensitiveCorsHeaderLookup(benchmark::State& state) {
    HttpRequest::HeaderMap headers = {
        {"access-control-allow-origin", "*"},
        {"access-control-allow-methods", "GET, POST, OPTIONS"},
        {"access-control-allow-headers", "content-type,x-grpc-web,authorization"},
        {"access-control-request-headers", "content-type,x-grpc-web"},
    };

    size_t idx = 0;
    for (auto _ : state) {
        auto it = headers.find(kCorsLookupKeys[idx & 3]);
        benchmark::DoNotOptimize(it);
        ++idx;
    }
}
BENCHMARK(BmCaseInsensitiveCorsHeaderLookup);

class LegacyHttpRouter {
  public:
    void AddRoute(HttpMethod method, std::string path, HttpHandler handler) {
        RouteHandler rh;
        rh.kind = RouteHandler::Kind::kUnary;
        rh.unary_handler = std::move(handler);
        AddRouteHandler(method, std::move(path), std::move(rh));
    }

    void AddStreamingRoute(HttpMethod method, std::string path, AsyncHttpHandler handler) {
        RouteHandler rh;
        rh.kind = RouteHandler::Kind::kStreaming;
        rh.streaming_handler = std::move(handler);
        AddRouteHandler(method, std::move(path), std::move(rh));
    }

    std::optional<RouteHandler> Match(HttpMethod method, std::string_view path) const {
        auto it = exact_routes_.find(path);
        if (it != exact_routes_.end()) {
            auto method_it = it->second.find(method);
            if (method_it != it->second.end()) {
                return method_it->second;
            }
            auto any_it = it->second.find(HttpMethod::kAny);
            if (any_it != it->second.end()) {
                return any_it->second;
            }
        }
        for (const auto& w : wildcard_routes_) {
            if ((w.method == method || w.method == HttpMethod::kAny) &&
                absl::StartsWith(path, w.prefix)) {
                return w.handler;
            }
        }
        return std::nullopt;
    }

    std::vector<std::string> GetAllowedMethods(std::string_view path) const {
        absl::flat_hash_set<std::string> allowed_set;
        auto exact_it = methods_by_exact_path_.find(path);
        if (exact_it != methods_by_exact_path_.end()) {
            for (HttpMethod m : exact_it->second) {
                allowed_set.insert(std::string(HttpMethodToString(m)));
            }
        }
        for (const auto& w : wildcard_routes_) {
            if (absl::StartsWith(path, w.prefix)) {
                allowed_set.insert(std::string(HttpMethodToString(w.method)));
            }
        }
        std::vector<std::string> allowed(allowed_set.begin(), allowed_set.end());
        std::sort(allowed.begin(), allowed.end());
        return allowed;
    }

  private:
    struct WildcardRoute {
        HttpMethod method;
        std::string prefix;
        RouteHandler handler;
    };

    void AddRouteHandler(HttpMethod method, std::string path, RouteHandler rh) {
        size_t star = path.find('*');
        if (star != std::string::npos) {
            WildcardRoute wr{method, path.substr(0, star), std::move(rh)};
            auto it = std::lower_bound(wildcard_routes_.begin(), wildcard_routes_.end(), wr,
                                       [](const WildcardRoute& a, const WildcardRoute& b) {
                                           return a.prefix.size() > b.prefix.size();
                                       });
            wildcard_routes_.insert(it, std::move(wr));
            return;
        }
        methods_by_exact_path_[path].insert(method);
        exact_routes_[std::move(path)].insert_or_assign(method, std::move(rh));
    }

    TransparentStringMap<absl::flat_hash_map<HttpMethod, RouteHandler>> exact_routes_;
    std::vector<WildcardRoute> wildcard_routes_;
    TransparentStringMap<absl::flat_hash_set<HttpMethod>> methods_by_exact_path_;
};

constexpr std::array<std::string_view, 4> kExactRoutePaths = {{
    "/api/v1/status",
    "/api/v1/devices",
    "/api/v1/metrics",
    "/healthz",
}};

template <typename RouterT>
void PopulateSampleRouter(RouterT& router) {
    auto shared_channel = std::make_shared<int>(42);
    auto shared_tracker = std::make_shared<int>(7);
    std::string allowed_origin = "http://localhost:8080,http://127.0.0.1:8080";

    for (std::string_view route : kExactRoutePaths) {
        router.AddRoute(HttpMethod::kGet, std::string(route),
                        [shared_channel, shared_tracker, allowed_origin](const HttpRequest&) {
                            return HttpResponse::Empty(200);
                        });
        router.AddRoute(HttpMethod::kPost, std::string(route),
                        [shared_channel, shared_tracker, allowed_origin](const HttpRequest&) {
                            return HttpResponse::Empty(200);
                        });
    }
    router.AddRoute(HttpMethod::kOptions, "/*",
                    [allowed_origin](const HttpRequest&) { return HttpResponse::Empty(204); });
    router.AddStreamingRoute(HttpMethod::kPost, "/*",
                             [shared_channel, allowed_origin, shared_tracker](
                                     const HttpRequest&, std::shared_ptr<HttpResponseWriter>) {});
}

void BmHttpRouterExactMatchBaseline(benchmark::State& state) {
    LegacyHttpRouter router;
    PopulateSampleRouter(router);

    size_t idx = 0;
    for (auto _ : state) {
        auto match = router.Match(HttpMethod::kGet, kExactRoutePaths[idx & 3]);
        benchmark::DoNotOptimize(match);
        ++idx;
    }
}
BENCHMARK(BmHttpRouterExactMatchBaseline);

void BmHttpRouterExactMatchHandler(benchmark::State& state) {
    HttpRouter router;
    PopulateSampleRouter(router);

    size_t idx = 0;
    for (auto _ : state) {
        const RouteHandler* match =
                router.MatchHandler(HttpMethod::kGet, kExactRoutePaths[idx & 3]);
        benchmark::DoNotOptimize(match);
        ++idx;
    }
}
BENCHMARK(BmHttpRouterExactMatchHandler);

void BmHttpRouterWildcardStreamMatchBaseline(benchmark::State& state) {
    LegacyHttpRouter router;
    PopulateSampleRouter(router);
    constexpr std::string_view kGrpcPath =
            "/android.emulation.control.EmulatorController/streamScreenshot";

    for (auto _ : state) {
        auto match = router.Match(HttpMethod::kPost, kGrpcPath);
        benchmark::DoNotOptimize(match);
    }
}
BENCHMARK(BmHttpRouterWildcardStreamMatchBaseline);

void BmHttpRouterWildcardStreamMatchHandler(benchmark::State& state) {
    HttpRouter router;
    PopulateSampleRouter(router);
    constexpr std::string_view kGrpcPath =
            "/android.emulation.control.EmulatorController/streamScreenshot";

    for (auto _ : state) {
        const RouteHandler* match = router.MatchHandler(HttpMethod::kPost, kGrpcPath);
        benchmark::DoNotOptimize(match);
    }
}
BENCHMARK(BmHttpRouterWildcardStreamMatchHandler);

void BmHttpRouterGetAllowedMethodsBaseline(benchmark::State& state) {
    LegacyHttpRouter router;
    PopulateSampleRouter(router);

    size_t idx = 0;
    for (auto _ : state) {
        auto allowed = router.GetAllowedMethods(kExactRoutePaths[idx & 3]);
        benchmark::DoNotOptimize(allowed);
        ++idx;
    }
}
BENCHMARK(BmHttpRouterGetAllowedMethodsBaseline);

void BmHttpRouterGetAllowedMethods(benchmark::State& state) {
    HttpRouter router;
    PopulateSampleRouter(router);

    size_t idx = 0;
    for (auto _ : state) {
        auto allowed = router.GetAllowedMethods(kExactRoutePaths[idx & 3]);
        benchmark::DoNotOptimize(allowed);
        ++idx;
    }
}
BENCHMARK(BmHttpRouterGetAllowedMethods);

}  // namespace
}  // namespace goldfish::http

BENCHMARK_MAIN();
