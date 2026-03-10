/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <vector>

#include "android/goldfish/hardware_config.h"
#include "goldfish/physics/physics.h"
#include "goldfish/sensors/foldable.h"
#include "goldfish/eventing/observable_value.h"

namespace goldfish::sensors {

class FoldableModel {
  public:
    using ObservablePosture = eventing::ObservableValue<FoldablePostures, eventing::ObservableValueTriggerAlways>;

    explicit FoldableModel(const android::goldfish::HardwareConfig& hw);

    // called by physical model to set hinge angle.
    void SetHingeAngle(uint32_t hinge_index, float degrees, PhysicalInterpolation mode);

    // called by physical model to set hinge posture.
    void SetPosture(float posture, PhysicalInterpolation mode);

    static void SetRollable(uint32_t index, float percentage, PhysicalInterpolation mode);

    float GetHingeAngle(uint32_t hinge_index, ParameterValueType parameter_value_type =
                                                      ParameterValueType::kCurrent) const;

    float GetRollable(uint32_t index, ParameterValueType parameter_value_type) const;

    float GetPosture(ParameterValueType parameter_value_type = ParameterValueType::kCurrent) const;

    FoldableState GetFoldableState() const { return state_; }  // structure copy

    bool IsFolded() const;

    static bool GetFoldedArea(int* x, int* y, int* w, int* h);

    ObservablePosture& GetPostureListener() { return posture_listener_; }

  private:
    void InitFoldableRoll(const android::goldfish::HardwareConfig& hw);
    void InitFoldableHinge(const android::goldfish::HardwareConfig& hw);

    FoldableState state_;
    std::vector<AnglesToPosture> angles_to_postures_;
    ObservablePosture posture_listener_;
};

}  // namespace goldfish::sensors
