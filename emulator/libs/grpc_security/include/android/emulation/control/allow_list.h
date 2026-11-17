// Copyright (C) 2022 The Android Open Source Project
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

#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/types/span.h"

namespace android::emulation::control {

// An AllowList can be used to configure access to gRPC uris.

class AllowList {
  public:
    using MethodList = std::vector<std::string_view>;
    virtual ~AllowList() = default;

    // Returns true if this url requires authentication
    virtual bool RequiresAuthentication(std::string_view path) = 0;

    // Returns true is this url occurs on an allow list in the
    // "allowed" section. This means it does not require an explicit
    // "aud" field with the given path.
    virtual bool IsAllowed(std::string_view sub, std::string_view path) = 0;

    // Returns true is this url occurs on an allow list in the
    // "protected" section. This means it requires an explicit
    // "aud" field with the given path.
    virtual bool IsProtected(std::string_view sub, std::string_view path) = 0;

    // Returns true if the given role grants permission to invoke `path` in the
    // "roles" section of the security specification (used for V2 APIs).
    virtual bool IsRoleAllowed(std::string_view role, std::string_view path) { return false; }

    // Returns true if any role in `roles` grants permission to invoke `path`.
    bool HasRolePermission(absl::Span<const std::string> roles, std::string_view path) {
        for (const auto& role : roles) {
            if (IsRoleAllowed(role, path)) {
                return true;
            }
        }
        return false;
    }

    bool IsRed(std::string_view sub, std::string_view path) {
        return !IsAllowed(sub, path) && !IsProtected(sub, path);
    }

    // Creates an allow list from the given json, note that bad json will
    // result in an allow list that denies everything to everyone.
    static std::unique_ptr<AllowList> FromJson(std::string_view json_with_comments);

    // Creates an allow list from the given stream, note that bad json will
    // result in an allow list that denies everything to everyone.
    static std::unique_ptr<AllowList> FromStream(std::istream& json_with_comments);

    std::string GetSource() { return source_; }

    void SetSource(std::string src) { source_ = std::move(src); }

  private:
    std::string source_;
};

// Nobody can do anything. Reject everyone.
class DisableAccess : public AllowList {
  public:
    bool RequiresAuthentication(std::string_view /*path*/) override { return true; };

    bool IsAllowed(std::string_view /*sub*/, std::string_view /*path*/) override { return false; }

    bool IsProtected(std::string_view /*sub*/, std::string_view /*path*/) override { return false; }

    bool IsRoleAllowed(std::string_view /*role*/, std::string_view /*path*/) override {
        return false;
    }
};

}  // namespace android::emulation::control
