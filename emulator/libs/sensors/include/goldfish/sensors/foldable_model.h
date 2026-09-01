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

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "android/goldfish/hardware_config.h"
#include "goldfish/eventing/observable_value.h"
#include "goldfish/physics/physics.h"
#include "goldfish/sensors/foldable.h"

namespace goldfish::sensors {

class FoldableModel {
    struct Private {};

  public:
    FoldableModel(FoldableConfig, Private);

    /**
     * @brief Factory method to create a FoldableModel instance.
     * @param hw The hardware configuration of the current AVD.
     * @return A std::unique_ptr containing the FoldableModel if the AVD config
     *         supports foldable/rollable sensor capabilities or resizable configs,
     *         otherwise nullptr.
     */
    static absl::StatusOr<std::unique_ptr<FoldableModel>> Create(
            const android::goldfish::HardwareConfig& hw);

    using ObservablePosture =
            eventing::ObservableValue<FoldablePostures, eventing::ObservableValueTriggerOnUpdate>;

    const FoldableConfig& GetFoldableConfig() const { return config_; }
    bool GetFoldedArea(int* x, int* y, int* w, int* h) const;
    FoldablePostures GetFoldablePosture() const { return current_posture_.GetValue(); }
    ObservablePosture& GetPostureListener() { return current_posture_; }

    bool IsFolded() const;

    float GetHingeAngle(uint32_t hinge_index,
                        ParameterValueType value_type = ParameterValueType::kCurrent) const;
    void SetHingeAngle(uint32_t hinge_index, float degrees, PhysicalInterpolation mode);

    float GetPosture(ParameterValueType parameter_value_type = ParameterValueType::kCurrent) const;
    void SetPosture(float posture, PhysicalInterpolation mode);

    float GetRollable(uint32_t index, ParameterValueType parameter_value_type) const;
    void SetRollable(uint32_t index, float percentage, PhysicalInterpolation mode);

  private:
    FoldablePostures CalcCurrentPosture() const;

    const FoldableConfig config_;

    float current_hinge_degrees_[kMaxHinges] = {};
    float current_rolled_percent_[kMaxRolls] = {};
    ObservablePosture current_posture_;
};

}  // namespace goldfish::sensors
