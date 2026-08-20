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
#include <cstdint>

namespace goldfish::sensors {

#define ANDROID_FOLDABLE_MAX_HINGES 3
#define ANDROID_FOLDABLE_MAX_ROLLS 2
#if ANDROID_FOLDABLE_MAX_HINGES > ANDROID_FOLDABLE_MAX_ROLLS
#define ANDROID_FOLDABLE_MAX_HINGES_ROLLS ANDROID_FOLDABLE_MAX_HINGES
#else
#define ANDROID_FOLDABLE_MAX_HINGES_ROLLS ANDROID_FOLDABLE_MAX_ROLLS
#endif
#define ANDROID_FOLDABLE_MAX_DISPLAY_REGIONS 3

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

    Angles angles[ANDROID_FOLDABLE_MAX_HINGES_ROLLS];
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
    int x, y, width, height;
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
    int num_hinges;
    FoldablePostures fold_at_posture;
    FoldableHingeParameters hinge_params[ANDROID_FOLDABLE_MAX_HINGES];

    // For rollables only
    int num_rolls;
    FoldablePostures resize_at_posture[ANDROID_FOLDABLE_MAX_DISPLAY_REGIONS];
    RollableParameters rollable_params[ANDROID_FOLDABLE_MAX_ROLLS];
};

struct FoldableState {
    float current_hinge_degrees[ANDROID_FOLDABLE_MAX_HINGES];
    float current_rolled_percent[ANDROID_FOLDABLE_MAX_ROLLS];
    FoldablePostures current_posture;
};

}  // namespace goldfish::sensors
