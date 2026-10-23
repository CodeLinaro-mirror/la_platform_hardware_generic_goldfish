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

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "benchmark/benchmark.h"

#include "goldfish/http/http_headers.h"
#include "goldfish/http/http_request.h"

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

}  // namespace
}  // namespace goldfish::http

BENCHMARK_MAIN();
