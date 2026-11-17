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
#include "android/emulation/control/allow_list.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/log/log.h"

#include "nlohmann/json.hpp"
#include "re2/re2.h"

namespace android::emulation::control {

enum class AllowGroup : uint8_t {
    kNone,    // No auth needed
    kGreen,   // Ok, if token present
    kYellow,  // Ok, if token present and proper aud claim.
    kRole,    // Ok, if token contains a role that matches the v2 endpoint.
};

using regex = re2::RE2;
using iss = std::string;

using AccessList = std::vector<std::unique_ptr<regex>>;
using AllowMap = std::unordered_map<iss, AccessList>;

using AllowSet = std::unordered_set<std::string>;
using AllowCache = std::unordered_map<iss, AllowSet>;

using SecurityMap = std::unordered_map<AllowGroup, AllowMap>;
using RejectionCache = std::unordered_map<AllowGroup, AllowCache>;
using AcceptCache = std::unordered_map<AllowGroup, AllowCache>;

using json = nlohmann::json;

#define DEBUG 0

#if DEBUG
#define DD(...) ALOGD(__VA_ARGS__)
#else
#define DD(...)
#endif

// An allowlist that will cache responses, making sure we can validate
// subjects quickly once they have tried to access an endpoint.
class CachingAllowList : public AllowList {
  public:
    static constexpr const char* kUnprotected = "__everyone__";

    explicit CachingAllowList(SecurityMap security_map) : security_map_(std::move(security_map)) {}

    bool RequiresAuthentication(std::string_view path) override {
        return !IsAllowed(AllowGroup::kNone, kUnprotected, path);
    };

    bool IsAllowed(std::string_view sub, std::string_view path) override {
        return IsAllowed(AllowGroup::kGreen, sub, path);
    }

    bool IsProtected(std::string_view sub, std::string_view path) override {
        return IsAllowed(AllowGroup::kYellow, sub, path);
    }

    bool IsRoleAllowed(std::string_view role, std::string_view path) override {
        return IsAllowed(AllowGroup::kRole, role, path);
    }

  private:
    // Inserts an entry into the cache, expunging a random element
    // in case of overflow.
    static void Insert(std::unordered_map<AllowGroup, AllowCache>& cache, AllowGroup color,
                       const std::string& iss, const std::string& path) {
        constexpr int kMaxUriCache = 256;
        // Prevent cache overflow..

        if (cache[color][iss].size() >= kMaxUriCache) {
            // Randomly remove an element
            auto& set = cache[color][iss];
            auto it = set.begin();
            std::advance(it, rand() % set.size());

            DD("Cache overflow, removing %s", it->c_str());
            set.erase(it);
        }

        cache[color][iss].insert(path);
    }

    bool IsAllowed(AllowGroup color, std::string_view sub_view, std::string_view path_view) {
        const std::lock_guard<std::mutex> lock(allow_check_);

        const std::string sub = std::string(sub_view.data(), sub_view.length());
        if (!security_map_[color].contains(sub)) {
            if (color == AllowGroup::kRole) {
                LOG(WARNING) << "Authorization check for endpoint '" << path_view
                             << "' failed because role '" << sub
                             << "' is not defined in the loaded gRPC security specification. "
                             << "Use a defined role (such as 'aemu.role.read') or add '" << sub
                             << "' to the 'roles' section of emulator_access.json.";
            } else {
                LOG(WARNING) << "Unknown subject " << sub << " is requesting access.";
            }
            return false;
        }

        const std::string path = std::string(path_view.data(), path_view.length());
        if (rejection_cache_[color][sub].contains(path)) {
            return false;
        }

        if (accept_cache_[color][sub].contains(path)) {
            return true;
        }

        for (const auto& regex : security_map_[color][sub]) {
            if (regex::FullMatch(path, *regex)) {
                DD("%s is in %s group, access to %s is allowed, "
                   "caching uri",
                   sub.c_str(), colorStr(color), path.c_str());
                Insert(accept_cache_, color, sub, path);
                return true;
            }
        }

        DD("%s is in %s group, access to %s is denied, "
           "caching uri",
           sub.c_str(), colorStr(color), path.c_str());
        Insert(rejection_cache_, color, sub, path);
        return false;
    }

    static const char* ColorStr(AllowGroup group) {
        switch (group) {
        case AllowGroup::kNone:
            return "unprotected";
        case AllowGroup::kGreen:
            return "allowed";
        case AllowGroup::kYellow:
            return "protected";
        case AllowGroup::kRole:
            return "role";
        }
    }

    std::mutex allow_check_;
    SecurityMap security_map_;
    RejectionCache rejection_cache_;
    AcceptCache accept_cache_;
};

namespace {
AccessList ParseAccessList(const json& regex_list) {
    AccessList access;
    for (const auto& entry : regex_list) {
        auto expr = entry.get<std::string>();
        auto re = std::make_unique<regex>(expr);
        if (!re->ok()) {
            LOG(WARNING) << "Ignoring invalid regex: " << expr << " error: " << re->error();
        } else {
            DD("   %s", re->pattern());
            access.push_back(std::move(re));
        }
    }
    return access;
}

void RecordJsonEvent(json::parse_event_t event, const json& parsed,
                     std::vector<AllowSet>& object_keys) {
    switch (event) {
    case json::parse_event_t::object_start:
        object_keys.emplace_back();
        return;
    case json::parse_event_t::key: {
        if (object_keys.empty()) {
            LOG(QFATAL) << "Failed to parse gRPC security specification: encountered JSON key "
                           "outside an object in emulator_access.json. Aborting startup because a "
                           "malformed security configuration leaves active gRPC permissions "
                           "ambiguous; fix the JSON object structure in emulator_access.json.";
        }
        const std::string& key = parsed.get_ref<const std::string&>();
        if (!object_keys.back().insert(key).second) {
            LOG(QFATAL) << "Failed to parse gRPC security specification: duplicate JSON key '"
                        << key
                        << "' encountered in emulator_access.json. Aborting startup because a "
                           "malformed security configuration leaves active gRPC permissions "
                           "ambiguous; merge or remove the duplicate key definition.";
        }
        return;
    }
    case json::parse_event_t::object_end:
        if (object_keys.empty()) {
            LOG(QFATAL) << "Failed to parse gRPC security specification: encountered unmatched "
                           "JSON object_end in emulator_access.json. Aborting startup because a "
                           "malformed security configuration leaves active gRPC permissions "
                           "ambiguous; fix the JSON object structure in emulator_access.json.";
        }
        object_keys.pop_back();
        return;
    default:
        return;
    }
}

template <typename InputType>
json ParseUniqueKeyJson(InputType&& input) {
    std::vector<AllowSet> object_keys;
    auto callback = [&object_keys](int /*depth*/, json::parse_event_t event, json& parsed) {
        RecordJsonEvent(event, parsed, object_keys);
        return true;
    };

    return json::parse(std::forward<InputType>(input), callback,
                       /*allow_exceptions=*/false, /*ignore_comments=*/true);
}

void ParseRolesObject(const json& roles_object, SecurityMap& secure) {
    if (!roles_object.is_object()) {
        LOG(WARNING) << "Failed to parse 'roles' in gRPC security specification: expected a JSON "
                        "object mapping role names to endpoint regex arrays, got "
                     << roles_object.type_name()
                     << ". Fix 'roles' in emulator_access.json to be a JSON object.";
        return;
    }

    AllowMap& role_map = secure[AllowGroup::kRole];
    for (const auto& [role, regex_list] : roles_object.items()) {
        if (!regex_list.is_array()) {
            LOG(WARNING) << "Failed to parse role '" << role
                         << "' in gRPC security specification: expected a JSON array of endpoint "
                            "regexes, got "
                         << regex_list.type_name()
                         << ". Update the role entry in emulator_access.json to map to a list of "
                            "regular expressions.";
            continue;
        }
        DD("Role list for role: %s", role.c_str());
        if (!role_map.try_emplace(role, ParseAccessList(regex_list)).second) {
            LOG(QFATAL) << "Failed to parse 'roles' in gRPC security specification: duplicate "
                           "role '"
                        << role
                        << "' encountered in emulator_access.json. Aborting startup because a "
                           "malformed security configuration leaves active gRPC permissions "
                           "ambiguous; merge or remove the duplicate role definition.";
        }
    }
}

std::unique_ptr<AllowList> ParseJsonObject(const json& json_object) {
    if (json_object.is_discarded()) {
        LOG(ERROR) << "The json is invalid, access disabled!";
        return std::make_unique<DisableAccess>();
    }

    SecurityMap secure;
    const std::vector<std::unique_ptr<regex>> unprotected;
    if (json_object.count("unprotected")) {
        DD("Open calls: ");
        secure[AllowGroup::kNone][CachingAllowList::kUnprotected] =
                ParseAccessList(json_object["unprotected"]);
    }

    if (json_object.contains("roles")) {
        ParseRolesObject(json_object["roles"], secure);
    }

    if (json_object.count("allowlist")) {
        for (const auto& entry : json_object["allowlist"]) {
            if (!entry.count("iss")) {
                LOG(WARNING) << "Invalid allow list. Missing \"iss\" claim, "
                                "skipping entry: "
                             << entry.dump(2);
                continue;
            }

            if (!entry.count("allowed") && !entry.count("protected")) {
                LOG(WARNING) << "Invalid allow list. Missing \"allowed\" and \"protected\" "
                                "list, skipping entry: "
                             << entry.dump(2).c_str();
                continue;
            }

            auto iss = entry["iss"].get<std::string>();

            if (iss == CachingAllowList::kUnprotected) {
                LOG(WARNING) << "Skipping " << CachingAllowList::kUnprotected
                             << ", this is a reserved issuer.";
                continue;
            }

            if (entry.count("allowed")) {
                DD("Green list for iss: %s", iss)
                secure[AllowGroup::kGreen][iss] = ParseAccessList(entry["allowed"]);
            }

            if (entry.count("protected")) {
                DD("Yellow list for iss: %s", iss);
                secure[AllowGroup::kYellow][iss] = ParseAccessList(entry["protected"]);
            }
        }
    }

    return std::make_unique<CachingAllowList>(std::move(secure));
}
}  // namespace

std::unique_ptr<AllowList> AllowList::FromJson(std::string_view json_with_comments) {
    return ParseJsonObject(ParseUniqueKeyJson(json_with_comments));
}

std::unique_ptr<AllowList> AllowList::FromStream(std::istream& json_with_comments) {
    if (!json_with_comments.good()) {
        LOG(WARNING) << "Unable to access file!";
    }
    return ParseJsonObject(ParseUniqueKeyJson(json_with_comments));
}

}  // namespace android::emulation::control
