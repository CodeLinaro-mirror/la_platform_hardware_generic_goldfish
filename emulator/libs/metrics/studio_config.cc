/* Copyright 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "goldfish/metrics/studio_config.h"

#include <fstream>
#include <string>

#include "nlohmann/json.hpp"

namespace goldfish::metrics::studio {

using json = nlohmann::json;

namespace {

fs::path GetAnalyticsSettingsPath(const fs::path& user_directory) {
    return user_directory / "analytics.settings";
}

absl::StatusOr<json> ParseAnalyticsSettingsJson(const fs::path& user_directory) {
    fs::path settings_path = GetAnalyticsSettingsPath(user_directory);
    std::ifstream is(settings_path);
    if (!is.is_open()) {
        return OptInState::kUnknown;
    }

    // Parse JSON without exceptions.
    json j = json::parse(is, /*callback=*/nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) {
        return OptInState::kUnknown;
    }
    return j;
}

} // namespace

OptInState GetUserMetricsOptIn(const fs::path& user_directory) {
    auto json = ParseAnalyticsSettingsJson(user_directory);
    if (!json.ok()) {
        return OptInState::kUnknown;
    }

    if (!json->contains("hasOptedIn")) {
        return OptInState::kUnknown;
    }

    const auto& val = (*json)["hasOptedIn"];
    bool opted_in = false;
    if (val.is_boolean()) {
        opted_in = val.get<bool>();
    } else if (val.is_number()) {
        opted_in = val.get<int>() != 0;
    } else if (val.is_string()) {
        std::string s = val.get<std::string>();
        opted_in = (s == "true" || s == "1");
    } else {
        return OptInState::kUnknown;
    }
    return opted_in ? OptInState::kOptedIn : OptInState::kOptedOut;
}

std::string GetMetricsUserId(const fs::path& user_directory) {
    auto json = ParseAnalyticsSettingsJson(user_directory);
    if (!json.ok()) {
        return {};
    }

    if (!json->contains("userId")) {
        return {};
    }

    const auto& val = (*json)["userId"];
    if (!val.is_string()) {
        return {};
    }
    return val.get<std::string>();
}

fs::path GetSpoolDirectory(const fs::path& user_directory) {
    return user_directory / "metrics" / "spool";
}

}  // namespace goldfish::metrics::studio
