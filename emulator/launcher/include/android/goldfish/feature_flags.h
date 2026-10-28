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
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"

#include "goldfish/metrics/studio_stats_wrapper.h"

namespace android::goldfish {

using EmulatorFeatureFlag = android_studio::EmulatorFeatureFlagState::EmulatorFeatureFlag;

// Evaluates and stores emulator feature flags from host/guest INI files and overrides.
class FeatureFlags {
  public:
    struct Options {
        std::filesystem::path host_ini_path;
        std::filesystem::path guest_ini_path;
        std::filesystem::path user_ini_path;
        int guest_api_level = 0;
        std::string_view avd_feature_flags;
        std::string_view env_feature_flags;
        std::span<const std::string> cli_feature_flags;
    };

    static absl::StatusOr<FeatureFlags> Create(const Options& options);
    static FeatureFlags CreateForTesting(
            std::initializer_list<EmulatorFeatureFlag> baseline_enabled = {});

    FeatureFlags();

    bool IsEnabled(EmulatorFeatureFlag feature) const;
    bool IsOverridden(EmulatorFeatureFlag feature) const;
    bool IsEnabledByGuest(EmulatorFeatureFlag feature) const;

    void SetEnabledOverride(EmulatorFeatureFlag feature, bool enabled);
    void SetIfNotOverridden(EmulatorFeatureFlag feature, bool enabled);

    std::vector<EmulatorFeatureFlag> GetEnabled() const;
    std::vector<EmulatorFeatureFlag> GetEnabledNonOverride() const;
    std::vector<EmulatorFeatureFlag> GetEnabledOverride() const;
    std::vector<EmulatorFeatureFlag> GetDisabledOverride() const;

  private:
    struct FeatureState {
        bool baseline_val = false;
        bool current_val = false;
        bool is_overridden = false;
        bool is_enabled_by_guest = false;
    };

    void ApplyOverrides(std::string_view overrides, std::string_view source = "");

    template <typename Predicate>
    std::vector<EmulatorFeatureFlag> CollectFeatures(Predicate predicate) const;

    absl::flat_hash_map<EmulatorFeatureFlag, FeatureState> features_;
};

}  // namespace android::goldfish
