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

#include <utility>

#include "absl/log/log.h"
#include "absl/strings/str_split.h"

namespace goldfish::sensors {

void FoldableModel::InitFoldableRoll(const android::goldfish::HardwareConfig& hw) {
    if (!hw.hw_sensor_roll) {
        state_.config.num_rolls = 0;
        return;
    }

    struct FoldableConfig& config = state_.config;
    auto type = static_cast<FoldableDisplayType>(hw.hw_sensor_hinge_type);
    if (type >= FoldableDisplayType::kTypeMax) {
        type = FoldableDisplayType::kHorizontalRoll;
    }
    config.type = type;

    // number
    int num_rolls = hw.hw_sensor_roll_count;
    if (num_rolls < 0 || num_rolls > ANDROID_FOLDABLE_MAX_ROLLS) {
        num_rolls = 0;
        LOG(WARNING) << "Incorrect roll count " << hw.hw_sensor_roll_count << ", default to 0";
    }
    config.num_rolls = num_rolls;

    // resize at postures
    config.resize_at_posture[0] = static_cast<enum FoldablePostures>(
            hw.hw_sensor_roll_resize_to_displayRegion_0_1_at_posture);
    config.resize_at_posture[1] = static_cast<enum FoldablePostures>(
            hw.hw_sensor_roll_resize_to_displayRegion_0_2_at_posture);
    config.resize_at_posture[2] = static_cast<enum FoldablePostures>(
            hw.hw_sensor_roll_resize_to_displayRegion_0_3_at_posture);

    // hinge angle ranges and defaults
    const std::string roll_ranges(hw.hw_sensor_roll_ranges);
    const std::string roll_defaults(hw.hw_sensor_roll_defaults);
    const std::string roll_radius(hw.hw_sensor_roll_radius);
    const std::string roll_direction(hw.hw_sensor_roll_direction);
    std::vector<std::string> roll_range_tokens = absl::StrSplit(roll_ranges, ',');
    std::vector<std::string> roll_default_tokens = absl::StrSplit(roll_defaults, ',');
    std::vector<std::string> roll_radius_tokens = absl::StrSplit(roll_radius, ',');
    std::vector<std::string> roll_direction_tokens = absl::StrSplit(roll_direction, ',');
    if (roll_range_tokens.size() != num_rolls || roll_default_tokens.size() != num_rolls ||
        roll_radius_tokens.size() != num_rolls || roll_direction_tokens.size() != num_rolls) {
        LOG(ERROR) << "Incorrect rollable configs for ranges " << roll_ranges << ", defaults "
                   << roll_defaults << ", radius " << roll_radius << ", or directions "
                   << roll_direction;

    } else {
        for (int i = 0; i < num_rolls; i++) {
            std::vector<std::string> range = absl::StrSplit(roll_range_tokens[i], '-');
            if (range.size() != 2) {
                LOG(ERROR) << "Incorrect rollable angle range " << roll_range_tokens[i];

            } else {
                config.rollable_params[i] = {
                    .roll_radius_as_display_percent = std::stof(roll_radius_tokens[i]),
                    .display_id = 0,  // TODO: put 0 for now
                    .min_rolled_percent = std::stof(range[0]),
                    .max_rolled_percent = std::stof(range[1]),
                    .default_rolled_percent = std::stof(roll_default_tokens[i]),
                    .direction = std::stoi(roll_direction_tokens[i]),
                };
            }
        }
    }
    for (unsigned int i = 0; std::cmp_less(i, state_.config.num_rolls); ++i) {
        state_.current_rolled_percent[i] = state_.config.rollable_params[i].default_rolled_percent;
    }
}

FoldableModel::FoldableModel(const android::goldfish::HardwareConfig& hw) {
    InitFoldableRoll(hw);
}

void FoldableModel::SetHingeAngle(uint32_t /*hinge_index*/, float /*degrees*/,
                                  PhysicalInterpolation /*mode*/, std::recursive_mutex& /*mutex*/) {
    LOG(WARNING) << "Not yet implemented";
}

void FoldableModel::SetPosture(float /*posture*/, PhysicalInterpolation /*mode*/,
                               std::recursive_mutex& /*mutex*/) {
    LOG(WARNING) << "Not yet implemented";
}

void FoldableModel::SetRollable(uint32_t /*index*/, float /*percentage*/,
                                PhysicalInterpolation /*mode*/, std::recursive_mutex& /*mutex*/) {
    LOG(WARNING) << "Not yet implemented";
}

float FoldableModel::GetHingeAngle(uint32_t hinge_index,
                                   ParameterValueType parameter_value_type) const {
    if (hinge_index >= ANDROID_FOLDABLE_MAX_HINGES) return 0.0F;
    if (std::cmp_greater_equal(hinge_index, state_.config.num_hinges)) return 0.0F;
    return parameter_value_type == ParameterValueType::kDefault
                   ? state_.config.hinge_params[hinge_index].default_degrees
                   : state_.current_hinge_degrees[hinge_index];
}

float FoldableModel::GetPosture(ParameterValueType parameter_value_type) const {
    return parameter_value_type == ParameterValueType::kDefault
                   ? static_cast<float>(FoldablePostures::kUnknown)
                   : static_cast<float>(state_.current_posture);
}

float FoldableModel::GetRollable(uint32_t index, ParameterValueType parameter_value_type) const {
    if (index >= ANDROID_FOLDABLE_MAX_ROLLS) return 0.0F;
    if (std::cmp_greater_equal(index, state_.config.num_rolls)) return 0.0F;

    return parameter_value_type == ParameterValueType::kDefault
                   ? state_.config.rollable_params[index].default_rolled_percent
                   : state_.current_rolled_percent[index];
}

bool FoldableModel::GetFoldedArea(int* /*x*/, int* /*y*/, int* /*w*/, int* /*h*/) {
    LOG(WARNING) << "Not yet implemented";
    return false;
}

bool FoldableModel::IsFolded() {
    LOG(WARNING) << "Not yet implemented";
    return false;
}

}  // namespace goldfish::sensors
