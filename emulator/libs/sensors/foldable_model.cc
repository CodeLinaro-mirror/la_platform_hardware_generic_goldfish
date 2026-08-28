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

#include "goldfish/sensors/foldable_model.h"

#include <string_view>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

#include "android/status/status_macros.h"

namespace goldfish::sensors {

FoldableModel::FoldableModel(FoldableConfig fcfg, Private) : config_(std::move(fcfg)) {
    for (unsigned int i = 0; i < config_.num_hinges; ++i) {
        current_hinge_degrees_[i] = config_.hinge_params[i].default_degrees;
    }

    for (unsigned int i = 0; i < config_.num_rolls; ++i) {
        current_rolled_percent_[i] = config_.rollable_params[i].default_rolled_percent;
    }

    current_posture_.SetValue(CalcCurrentPosture());
}

absl::StatusOr<std::unique_ptr<FoldableModel>> FoldableModel::Create(
        const android::goldfish::HardwareConfig& hw) {
    if (!hw.hw_sensor_hinge && !hw.hw_sensor_roll) {
        return std::unique_ptr<FoldableModel>();
    }

    ASSIGN_OR_RETURN(auto fcfg, MakeFoldableConfig(hw));

    return std::make_unique<FoldableModel>(std::move(fcfg), Private());
}

FoldablePostures FoldableModel::CalcCurrentPosture() const {
    for (const auto& atp : config_.angles_to_postures) {
        bool match = true;
        for (unsigned i = 0; i < config_.num_hinges; ++i) {
            if (current_hinge_degrees_[i] < atp.angles[i].left ||
                current_hinge_degrees_[i] > atp.angles[i].right) {
                match = false;
                break;
            }
        }

        if (match) {
            return atp.posture;
        }
    }

    return FoldablePostures::kUnknown;
}

float FoldableModel::GetHingeAngle(uint32_t hinge_index,
                                   ParameterValueType parameter_value_type) const {
    DCHECK(config_.num_hinges <= kMaxHinges);
    if (hinge_index >= config_.num_hinges) return 0.0F;

    return parameter_value_type == ParameterValueType::kDefault
                   ? config_.hinge_params[hinge_index].default_degrees
                   : current_hinge_degrees_[hinge_index];
}

void FoldableModel::SetHingeAngle(uint32_t hinge_index, float degree,
                                  PhysicalInterpolation /*mode*/) {
    if (hinge_index < config_.num_hinges) {
        current_hinge_degrees_[hinge_index] = degree;

        const FoldablePostures posture = CalcCurrentPosture();
        if (posture != FoldablePostures::kUnknown) {
            current_posture_.SetValue(posture);
        }
    }
}

float FoldableModel::GetPosture(ParameterValueType parameter_value_type) const {
    return parameter_value_type == ParameterValueType::kDefault
                   ? static_cast<float>(FoldablePostures::kUnknown)
                   : static_cast<float>(current_posture_.GetValue());
}

void FoldableModel::SetPosture(float posture_float, PhysicalInterpolation /*mode*/) {
    const FoldablePostures posture = static_cast<FoldablePostures>(posture_float);

    for (const auto& atp : config_.angles_to_postures) {
        if (atp.posture == posture) {
            for (unsigned i = 0; i < config_.num_hinges; ++i) {
                current_hinge_degrees_[i] = atp.angles[i].default_value;
            }

            current_posture_.SetValue(posture);
            break;
        }
    }
}

void FoldableModel::SetRollable(uint32_t /*index*/, float /*percentage*/,
                                PhysicalInterpolation /*mode*/) {}

float FoldableModel::GetRollable(uint32_t index, ParameterValueType parameter_value_type) const {
    DCHECK(config_.num_rolls <= kMaxRolls);
    if (index >= config_.num_rolls) return 0.0F;

    return parameter_value_type == ParameterValueType::kDefault
                   ? config_.rollable_params[index].default_rolled_percent
                   : current_rolled_percent_[index];
}

bool FoldableModel::GetFoldedArea(int* x, int* y, int* w, int* h) const {
    if (x) *x = config_.folded_x;
    if (y) *y = config_.folded_y;
    if (w) *w = config_.folded_w;
    if (h) *h = config_.folded_h;
    return ((config_.folded_w > 0) && (config_.folded_h > 0));
}

bool FoldableModel::IsFolded() const {
    return current_posture_.GetValue() == FoldablePostures::kClosed;
}

}  // namespace goldfish::sensors
