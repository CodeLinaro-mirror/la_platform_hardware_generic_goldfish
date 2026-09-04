// Copyright 2024 The Android Open Source Project
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
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"

#include "android/goldfish/hardware_config.h"

namespace goldfish::sensors {

constexpr size_t kMaxHinges = 3;
constexpr size_t kMaxRolls = 2;
constexpr size_t kMaxHingesRolls = std::max(kMaxHinges, kMaxRolls);
constexpr size_t kMaxDisplayRegions = 3;

enum class FoldablePostures : uint8_t {
    kUnknown = 0,
    kClosed = 1,
    kHalfOpened = 2,
    kOpened = 3,
    kFlipped = 4,
    kTent = 5,
    kPostureMax = 6,
};

struct AnglesToPosture {
    struct Angles {
        float left;
        float right;
        float default_value;
    };

    Angles angles[kMaxHingesRolls];
    FoldablePostures posture;
};

enum class FoldableDisplayType : uint8_t {
    // Horizontal split means something like a laptop, i.e.
    // |-----| Camera is here
    // | top |
    // |-----| hinge 0
    // |     |
    // |-----| hinge 1
    // |     |
    // |-----|
    kHorizontalSplit = 0,

    // Vertical split is left to right, rotated version of horizontal split:
    // |-camera|-------|------|-------|
    // |       |       |      |       |
    // |       |       |      |       |
    // |-------|-------|------|-------|
    // hinge:  0       1      2
    // |-----|
    // | top |
    // |-----| hinge 0
    // |     |
    // |-----| hinge 1
    // |     |
    // |-----|
    kVerticalSplit = 1,

    // Roll configurations (essentially the # hinges are infinite,
    // representable via separate parameters)
    kHorizontalRoll = 2,
    kVerticalRoll = 3,
    kTypeMax = 4
};

struct FoldableHingeParameters {
    unsigned x, y, width, height;
    int display_id;
    float min_degrees;
    float max_degrees;
    float default_degrees;
};

struct RollableParameters {
    float roll_radius_as_display_percent;  // % of display height (horiz. roll) or display width
                                           // (vertical roll) that determines the radius of the
                                           // rollable
    int display_id;
    float min_rolled_percent;
    float max_rolled_percent;
    float default_rolled_percent;
    int direction;
};

struct FoldableConfig {
    FoldableDisplayType type;

    // For hinges only
    unsigned num_hinges = 0;
    int folded_x;
    int folded_y;
    int folded_w;
    int folded_h;
    FoldablePostures fold_at_posture;
    FoldableHingeParameters hinge_params[kMaxHinges];
    std::vector<AnglesToPosture> angles_to_postures;

    // For rollables only
    unsigned num_rolls = 0;
    FoldablePostures resize_at_posture[kMaxDisplayRegions];
    RollableParameters rollable_params[kMaxRolls];
};

absl::StatusOr<FoldableConfig> MakeFoldableConfig(const android::goldfish::HardwareConfig&);

}  // namespace goldfish::sensors
