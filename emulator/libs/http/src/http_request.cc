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
#include "goldfish/http/http_request.h"

#include <string>
#include <string_view>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"

namespace goldfish::http {

std::string_view HttpMethodToString(HttpMethod method) {
    switch (method) {
    case HttpMethod::kGet:
        return methods::kGet;
    case HttpMethod::kPost:
        return methods::kPost;
    case HttpMethod::kPut:
        return methods::kPut;
    case HttpMethod::kDelete:
        return methods::kDelete;
    case HttpMethod::kOptions:
        return methods::kOptions;
    case HttpMethod::kHead:
        return methods::kHead;
    case HttpMethod::kPatch:
        return methods::kPatch;
    case HttpMethod::kAny:
        return methods::kAny;
    }
    return "UNKNOWN";
}

std::optional<HttpMethod> StringToHttpMethod(std::string_view method) {
    if (absl::EqualsIgnoreCase(method, methods::kGet)) {
        return HttpMethod::kGet;
    }
    if (absl::EqualsIgnoreCase(method, methods::kPost)) {
        return HttpMethod::kPost;
    }
    if (absl::EqualsIgnoreCase(method, methods::kPut)) {
        return HttpMethod::kPut;
    }
    if (absl::EqualsIgnoreCase(method, methods::kDelete)) {
        return HttpMethod::kDelete;
    }
    if (absl::EqualsIgnoreCase(method, methods::kOptions)) {
        return HttpMethod::kOptions;
    }
    if (absl::EqualsIgnoreCase(method, methods::kHead)) {
        return HttpMethod::kHead;
    }
    if (absl::EqualsIgnoreCase(method, methods::kPatch)) {
        return HttpMethod::kPatch;
    }
    if (method == methods::kAny) {
        return HttpMethod::kAny;
    }
    return std::nullopt;
}

std::string_view HttpRequest::GetHeader(std::string_view name) const {
    if (name.empty()) {
        return {};
    }
    auto it = headers_.find(name);
    if (it != headers_.end()) {
        return it->second;
    }
    return {};
}

bool HttpRequest::HasHeader(std::string_view name) const {
    if (name.empty()) {
        return false;
    }
    return headers_.contains(name);
}

}  // namespace goldfish::http
