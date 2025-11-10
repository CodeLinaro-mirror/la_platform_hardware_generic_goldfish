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

namespace goldfish::sensors {

#define ANDROID_FOLDABLE_MAX_HINGES 3
#define ANDROID_FOLDABLE_MAX_ROLLS 2
#if ANDROID_FOLDABLE_MAX_HINGES > ANDROID_FOLDABLE_MAX_ROLLS
#define ANDROID_FOLDABLE_MAX_HINGES_ROLLS ANDROID_FOLDABLE_MAX_HINGES
#else
#define ANDROID_FOLDABLE_MAX_HINGES_ROLLS ANDROID_FOLDABLE_MAX_ROLLS
#endif
#define ANDROID_FOLDABLE_MAX_DISPLAY_REGIONS 3

enum class FoldablePostures {
  UNKNOWN = 0,
  CLOSED = 1,
  HALF_OPENED = 2,
  OPENED = 3,
  FLIPPED = 4,
  TENT = 5,
  POSTURE_MAX
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

enum class FoldableDisplayType {
  // Horizontal split means something like a laptop, i.e.
  // |-----| Camera is here
  // | top |
  // |-----| hinge 0
  // |     |
  // |-----| hinge 1
  // |     |
  // |-----|
  HORIZONTAL_SPLIT = 0,

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
  VERTICAL_SPLIT = 1,

  // Roll configurations (essentially the # hinges are infinite,
  // representable via separate parameters)
  HORIZONTAL_ROLL = 2,
  VERTICAL_ROLL = 3,
  TYPE_MAX
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
  FoldableDisplayType type;

  // For hinges only
  int numHinges;
  FoldablePostures foldAtPosture;
  FoldableHingeParameters hingeParams[ANDROID_FOLDABLE_MAX_HINGES];

  // For rollables only
  int numRolls;
  FoldablePostures resizeAtPosture[ANDROID_FOLDABLE_MAX_DISPLAY_REGIONS];
  RollableParameters rollableParams[ANDROID_FOLDABLE_MAX_ROLLS];
};

struct FoldableState {
  FoldableConfig config;
  float currentHingeDegrees[ANDROID_FOLDABLE_MAX_HINGES];
  float currentRolledPercent[ANDROID_FOLDABLE_MAX_ROLLS];
  FoldablePostures currentPosture;
};

}  // namespace goldfish::sensors
