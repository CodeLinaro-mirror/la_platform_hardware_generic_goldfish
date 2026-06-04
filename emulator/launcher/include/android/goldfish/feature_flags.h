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

#pragma once

#include <filesystem>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"

#include "goldfish/metrics/studio_stats_wrapper.h"

namespace android::goldfish {

enum class FeatureStatus {
    Missing,
    On,
    Off,
};

using FeatureFlagMap =
        absl::flat_hash_map<android_studio::EmulatorFeatureFlagState::EmulatorFeatureFlag,
                            FeatureStatus>;

/**
 * @brief Parses a feature file (usually advancedFeatures.ini) and maps the
 * contents to the EmulatorFeatureFlag enum.
 *
 * @param filepath The path to the feature file to parse.
 * @return A map of feature flags to their status, or an error status.
 */
absl::StatusOr<FeatureFlagMap> ParseFeatureFile(const std::filesystem::path& filepath);

}  // namespace android::goldfish
