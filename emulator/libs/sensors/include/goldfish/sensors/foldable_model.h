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
    FoldableModel(const android::goldfish::HardwareConfig& hw, Private);

    /**
     * @brief Factory method to create a FoldableModel instance.
     * @param hw The hardware configuration of the current AVD.
     * @return A std::unique_ptr containing the FoldableModel if the AVD config
     *         supports foldable/rollable sensor capabilities or resizable configs,
     *         otherwise nullptr.
     */
    static std::unique_ptr<FoldableModel> Create(const android::goldfish::HardwareConfig& hw);

    using ObservablePosture =
            eventing::ObservableValue<FoldablePostures, eventing::ObservableValueTriggerAlways>;

    struct ResizableConfig {
        std::string name;
        uint32_t id = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t dpi = 0;

        bool operator==(const ResizableConfig& rhs) const;
    };

    static std::optional<std::vector<ResizableConfig>> ParseResizableConfigs(std::string_view);
    // called by physical model to set hinge angle.
    void SetHingeAngle(uint32_t hinge_index, float degrees, PhysicalInterpolation mode);

    // called by physical model to set hinge posture.
    void SetPosture(float posture, PhysicalInterpolation mode);

    void SetRollable(uint32_t index, float percentage, PhysicalInterpolation mode);

    float GetHingeAngle(uint32_t hinge_index, ParameterValueType parameter_value_type =
                                                      ParameterValueType::kCurrent) const;

    float GetRollable(uint32_t index, ParameterValueType parameter_value_type) const;

    float GetPosture(ParameterValueType parameter_value_type = ParameterValueType::kCurrent) const;

    const FoldableConfig& GetFoldableConfig() const { return config_; }
    const FoldableState& GetFoldableState() const { return state_; }

    bool IsFolded() const;

    bool GetFoldedArea(int* x, int* y, int* w, int* h) const;

    const std::vector<ResizableConfig>& GetResizableConfigs() const { return resizable_configs_; }

    ObservablePosture& GetPostureListener() { return posture_listener_; }

  private:
    void InitFoldableRoll(const android::goldfish::HardwareConfig& hw);
    void InitFoldableHinge(const android::goldfish::HardwareConfig& hw);
    void InitResizableConfigs(const android::goldfish::HardwareConfig& hw);

    FoldableConfig config_ = {};
    std::vector<AnglesToPosture> angles_to_postures_;
    std::vector<ResizableConfig> resizable_configs_;
    FoldableState state_ = {};
    ObservablePosture posture_listener_;

    int folded_x_ = 0;
    int folded_y_ = 0;
    int folded_w_ = 0;
    int folded_h_ = 0;
};

}  // namespace goldfish::sensors
