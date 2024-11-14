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
/* Foldable state */

#define ANDROID_FOLDABLE_MAX_HINGES 3
#define ANDROID_FOLDABLE_MAX_ROLLS 2
#if ANDROID_FOLDABLE_MAX_HINGES > ANDROID_FOLDABLE_MAX_ROLLS
#define ANDROID_FOLDABLE_MAX_HINGES_ROLLS ANDROID_FOLDABLE_MAX_HINGES
#else
#define ANDROID_FOLDABLE_MAX_HINGES_ROLLS ANDROID_FOLDABLE_MAX_ROLLS
#endif
#define ANDROID_FOLDABLE_MAX_DISPLAY_REGIONS 3

enum FoldablePostures {
    POSTURE_UNKNOWN = 0,
    POSTURE_CLOSED = 1,
    POSTURE_HALF_OPENED = 2,
    POSTURE_OPENED = 3,
    POSTURE_FLIPPED = 4,
    POSTURE_TENT = 5,
    POSTURE_MAX
};

struct AnglesToPosture {
    struct {
        float left;
        float right;
        float default_value;
    } angles[ANDROID_FOLDABLE_MAX_HINGES_ROLLS];
    enum FoldablePostures posture;
};

enum FoldableDisplayType {
    // Horizontal split means something like a laptop, i.e.
    // |-----| Camera is here
    // | top |
    // |-----| hinge 0
    // |     |
    // |-----| hinge 1
    // |     |
    // |-----|
    ANDROID_FOLDABLE_HORIZONTAL_SPLIT = 0,

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
    ANDROID_FOLDABLE_VERTICAL_SPLIT = 1,

    // Roll configurations (essentially the # hinges are infinite,
    // representable via separate parameters)
    ANDROID_FOLDABLE_HORIZONTAL_ROLL = 2,
    ANDROID_FOLDABLE_VERTICAL_ROLL = 3,
    ANDROID_FOLDABLE_TYPE_MAX
};

enum FoldableHingeSubType {
    ANDROID_FOLDABLE_HINGE_FOLD = 0,
    ANDROID_FOLDABLE_HINGE_HINGE = 1,
    ANDROID_FOLDABLE_HINGE_SUB_TYPE_MAX
};

struct FoldableHingeParameters {
    int x, y, width, height;
    int displayId;
    float minDegrees;
    float maxDegrees;
    float defaultDegrees;
};

struct RollableParameters {
    float rollRadiusAsDisplayPercent;  // % of display height (horiz. roll) or display width
                                       // (vertical roll) that determines the radius of the rollable
    int displayId;
    float minRolledPercent;
    float maxRolledPercent;
    float defaultRolledPercent;
    int direction;
};

struct FoldableConfig {
    enum FoldableDisplayType type;
    enum FoldableHingeSubType hingesSubType;
    bool supportedFoldablePostures[POSTURE_MAX];

    // For hinges only
    int numHinges;
    enum FoldablePostures foldAtPosture;
    struct FoldableHingeParameters hingeParams[ANDROID_FOLDABLE_MAX_HINGES];
    // For rollables only
    int numRolls;
    enum FoldablePostures resizeAtPosture[ANDROID_FOLDABLE_MAX_DISPLAY_REGIONS];
    struct RollableParameters rollableParams[ANDROID_FOLDABLE_MAX_ROLLS];
};

struct FoldableState {
    struct FoldableConfig config;
    float currentHingeDegrees[ANDROID_FOLDABLE_MAX_HINGES];
    float currentRolledPercent[ANDROID_FOLDABLE_MAX_ROLLS];
    enum FoldablePostures currentPosture;
};

/* TODO(b/376892957): No foldable support yet.
int android_foldable_get_posture();
int android_foldable_get_state(struct FoldableState* state);
bool android_foldable_hinge_configured();
bool android_foldable_is_pixel_fold();
int android_foldable_pixel_fold_second_display_id();
bool android_foldable_hinge_enabled();
bool android_foldable_any_folded_area_configured();
bool android_foldable_folded_area_configured(int area);
bool android_foldable_is_folded();
bool android_foldable_fold();
bool android_foldable_unfold();
bool android_foldable_set_posture(int posture);
bool android_foldable_get_folded_area(int* x, int* y, int* w, int* h);
bool android_foldable_rollable_configured();
bool android_hw_sensors_is_loading_snapshot();
bool android_heart_rate_sensor_configured();
bool android_foldable_posture_name(int posture, char* name);
bool android_is_automotive();
void* android_get_posture_listener();
*/