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
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/strip.h"

namespace goldfish::http {

namespace {

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

void HttpRouter::AddRouteHandler(HttpMethod method, std::string path, RouteHandler rh) {
    if (IsWildcardPath(path)) {
        WildcardRoute wr{method, ExtractPrefix(path), std::move(rh)};
        // Keep wildcard_routes_ sorted by descending prefix size using binary search insertion.
        auto it = std::lower_bound(wildcard_routes_.begin(), wildcard_routes_.end(), wr,
                                   [](const WildcardRoute& a, const WildcardRoute& b) {
                                       return a.prefix.size() > b.prefix.size();
                                   });
        wildcard_routes_.insert(it, std::move(wr));
    } else {
        methods_by_exact_path_[path].insert(method);
        exact_routes_[std::move(path)].insert_or_assign(method, std::move(rh));
    }
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

std::optional<RouteHandler> HttpRouter::Match(HttpMethod method, std::string_view path) const {
    // 1. Exact match lookup (Zero-allocation string_view heterogeneous find)
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

    // 2. Wildcard / Prefix routes (ordered by longest prefix)
    for (const auto& w : wildcard_routes_) {
        if (w.method == method || w.method == HttpMethod::kAny) {
            if (absl::StartsWith(path, w.prefix)) {
                return w.handler;
            }
        }
    }

    return std::nullopt;
}

std::vector<std::string> HttpRouter::GetAllowedMethods(std::string_view path) const {
    absl::flat_hash_set<std::string> allowed_set;

    auto record_method = [&](HttpMethod m) -> bool {
        if (m == HttpMethod::kAny) {
            return true;
        }
        allowed_set.insert(std::string(HttpMethodToString(m)));
        return false;
    };

    auto exact_it = methods_by_exact_path_.find(path);
    if (exact_it != methods_by_exact_path_.end()) {
        for (HttpMethod m : exact_it->second) {
            if (record_method(m)) {
                return AllMethods();
            }
        }
    }

    for (const auto& w : wildcard_routes_) {
        if (absl::StartsWith(path, w.prefix)) {
            if (record_method(w.method)) {
                return AllMethods();
            }
        }
    }

    std::vector<std::string> allowed(allowed_set.begin(), allowed_set.end());
    std::sort(allowed.begin(), allowed.end());
    return allowed;
}

}  // namespace goldfish::http
