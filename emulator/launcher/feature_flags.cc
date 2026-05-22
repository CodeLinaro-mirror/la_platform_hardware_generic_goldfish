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

#include "android/goldfish/feature_flags.h"

#include <fstream>
#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"

namespace android::goldfish {

namespace {

std::string FeatureEnumToString(
        android_studio::EmulatorFeatureFlagState::EmulatorFeatureFlag feature) {
    auto is_not_underscore = [](char c) { return c != '_'; };

    auto to_lower = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };

    auto feature_name = android_studio::EmulatorFeatureFlagState::EmulatorFeatureFlag_Name(feature);
    return feature_name | std::views::filter(is_not_underscore) | std::views::transform(to_lower) |
           std::ranges::to<std::string>();
}

const absl::flat_hash_map<std::string,
                          android_studio::EmulatorFeatureFlagState::EmulatorFeatureFlag>&
LoadFeatures() {
    static auto features = [] {
        absl::flat_hash_map<std::string,
                            android_studio::EmulatorFeatureFlagState::EmulatorFeatureFlag>
                f;
        for (int i = android_studio::EmulatorFeatureFlagState::EmulatorFeatureFlag_MIN;
             i <= android_studio::EmulatorFeatureFlagState::EmulatorFeatureFlag_MAX; ++i) {
            if (!android_studio::EmulatorFeatureFlagState::EmulatorFeatureFlag_IsValid(i)) {
                continue;
            }
            auto feature =
                    static_cast<android_studio::EmulatorFeatureFlagState::EmulatorFeatureFlag>(i);
            f[FeatureEnumToString(feature)] = feature;
        }
        return f;
    }();
    return features;
}

FeatureStatus StringToFeatureStatus(std::string_view status_str) {
    if (absl::EqualsIgnoreCase(status_str, "on")) {
        return FeatureStatus::On;
    } else if (absl::EqualsIgnoreCase(status_str, "off")) {
        return FeatureStatus::Off;
    }
    return FeatureStatus::Missing;
}

}  // namespace

absl::StatusOr<FeatureFlagMap> ParseFeatureFile(const std::filesystem::path& filepath) {
    auto features = LoadFeatures();

    std::ifstream file(filepath);
    if (!file.is_open()) {
        return absl::NotFoundError(
                absl::StrCat("Could not open feature file: ", filepath.string()));
    }

    FeatureFlagMap f;
    std::string line;
    int line_num = 0;
    while (std::getline(file, line)) {
        line_num++;
        std::string_view trimmed_line = absl::StripAsciiWhitespace(line);
        // Skip empty lines and lines starting with '#' (comments).
        if (trimmed_line.empty() || trimmed_line[0] == '#') {
            continue;
        }

        std::pair<std::string_view, std::string_view> parts = absl::StrSplit(trimmed_line, "=");
        std::string_view name = absl::StripAsciiWhitespace(parts.first);
        std::string_view status = absl::StripAsciiWhitespace(parts.second);
        if (name.empty()) {
            LOG(WARNING) << "Malformed line " << line_num
                         << " in feature file (empty feature name): '" << trimmed_line << "'";
            continue;
        }

        if (status.empty()) {
            LOG(WARNING) << "Malformed line " << line_num
                         << " in feature file (missing or empty feature status): '" << trimmed_line
                         << "'";
            continue;
        }

        if (auto i = features.find(absl::AsciiStrToLower(name)); i != features.end()) {
            VLOG(1) << "found feature: " << name << " " << status;
            f[i->second] = StringToFeatureStatus(status);
        } else {
            VLOG(1) << "unknown feature: " << name;
        }
    }

    return f;
}

}  // namespace android::goldfish
