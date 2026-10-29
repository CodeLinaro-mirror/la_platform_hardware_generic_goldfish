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

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/match.h"

namespace goldfish::http {

namespace {

static_assert(static_cast<uint8_t>(HttpMethod::kAny) < 8,
              "HttpMethod values must fit within an 8-bit method_mask.");

constexpr uint8_t MethodBit(HttpMethod method) noexcept {
    return static_cast<uint8_t>(uint8_t{1} << static_cast<uint8_t>(method));
}

constexpr std::array<std::pair<HttpMethod, std::string_view>, 7> kSortedMethods = {{
    {HttpMethod::kDelete, methods::kDelete},
    {HttpMethod::kGet, methods::kGet},
    {HttpMethod::kHead, methods::kHead},
    {HttpMethod::kOptions, methods::kOptions},
    {HttpMethod::kPatch, methods::kPatch},
    {HttpMethod::kPost, methods::kPost},
    {HttpMethod::kPut, methods::kPut},
}};

const std::vector<std::string>& AllMethods() {
    static const auto* const kAll = new std::vector<std::string>{
        std::string(methods::kGet),    std::string(methods::kPost),    std::string(methods::kPut),
        std::string(methods::kDelete), std::string(methods::kOptions), std::string(methods::kHead),
        std::string(methods::kPatch)};
    return *kAll;
}

bool IsWildcardPath(std::string_view path) {
    return path.find('*') != std::string_view::npos;
}

std::string ExtractPrefix(std::string_view wildcard_path) {
    size_t star = wildcard_path.find('*');
    if (star == std::string_view::npos) {
        return std::string(wildcard_path);
    }
    return std::string(wildcard_path.substr(0, star));
}

}  // namespace

const RouteHandler* HttpRouter::ExactRouteEntry::Find(HttpMethod method) const noexcept {
    if ((method_mask & MethodBit(method)) == 0) {
        return nullptr;
    }
    if (has_primary && primary_method == method) {
        return &primary_handler;
    }
    for (const auto& [registered_method, handler] : extra_handlers) {
        if (registered_method == method) {
            return &handler;
        }
    }
    return nullptr;
}

void HttpRouter::AddRouteHandler(HttpMethod method, std::string path, RouteHandler rh) {
    if (IsWildcardPath(path)) {
        WildcardRoute wr{method, ExtractPrefix(path), std::move(rh)};
        // Keep wildcard_routes_ sorted by descending prefix size using binary search insertion.
        auto it = std::lower_bound(wildcard_routes_.begin(), wildcard_routes_.end(), wr,
                                   [](const WildcardRoute& a, const WildcardRoute& b) {
                                       return a.prefix.size() > b.prefix.size();
                                   });
        wildcard_routes_.insert(it, std::move(wr));
        return;
    }

    ExactRouteEntry& entry = exact_routes_[std::move(path)];
    entry.method_mask |= MethodBit(method);
    if (!entry.has_primary || entry.primary_method == method) {
        entry.primary_method = method;
        entry.primary_handler = std::move(rh);
        entry.has_primary = true;
        return;
    }
    for (auto& [existing_method, existing_handler] : entry.extra_handlers) {
        if (existing_method == method) {
            existing_handler = std::move(rh);
            return;
        }
    }
    entry.extra_handlers.emplace_back(method, std::move(rh));
}

void HttpRouter::AddRoute(HttpMethod method, std::string path, HttpHandler handler) {
    RouteHandler rh;
    rh.kind = RouteHandler::Kind::kUnary;
    rh.unary_handler = std::move(handler);
    AddRouteHandler(method, std::move(path), std::move(rh));
}

void HttpRouter::AddStreamingRoute(HttpMethod method, std::string path, AsyncHttpHandler handler) {
    RouteHandler rh;
    rh.kind = RouteHandler::Kind::kStreaming;
    rh.streaming_handler = std::move(handler);
    AddRouteHandler(method, std::move(path), std::move(rh));
}

const RouteHandler* HttpRouter::MatchHandler(HttpMethod method, std::string_view path) const {
    // 1. Exact match lookup (Zero-allocation string_view heterogeneous find)
    auto it = exact_routes_.find(path);
    if (it != exact_routes_.end()) {
        if (const RouteHandler* exact = it->second.Find(method)) {
            return exact;
        }
        if (const RouteHandler* any = it->second.Find(HttpMethod::kAny)) {
            return any;
        }
    }

    // 2. Wildcard / Prefix routes (ordered by longest prefix)
    for (const auto& w : wildcard_routes_) {
        if ((w.method == method || w.method == HttpMethod::kAny) &&
            absl::StartsWith(path, w.prefix)) {
            return &w.handler;
        }
    }

    return nullptr;
}

std::optional<RouteHandler> HttpRouter::Match(HttpMethod method, std::string_view path) const {
    if (const RouteHandler* handler = MatchHandler(method, path)) {
        return *handler;
    }
    return std::nullopt;
}

std::vector<std::string> HttpRouter::GetAllowedMethods(std::string_view path) const {
    uint8_t mask = 0;

    auto exact_it = exact_routes_.find(path);
    if (exact_it != exact_routes_.end()) {
        mask |= exact_it->second.method_mask;
        if ((mask & MethodBit(HttpMethod::kAny)) != 0) {
            return AllMethods();
        }
    }

    for (const auto& w : wildcard_routes_) {
        if (!absl::StartsWith(path, w.prefix)) {
            continue;
        }
        if (w.method == HttpMethod::kAny) {
            return AllMethods();
        }
        mask |= MethodBit(w.method);
    }

    if (mask == 0) {
        return {};
    }

    std::vector<std::string> allowed;
    allowed.reserve(static_cast<size_t>(std::popcount(mask)));
    for (const auto& [method, name] : kSortedMethods) {
        if ((mask & MethodBit(method)) != 0) {
            allowed.emplace_back(name);
        }
    }
    return allowed;
}

}  // namespace goldfish::http
