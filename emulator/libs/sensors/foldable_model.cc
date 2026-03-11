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
        LOG(FATAL) << "Incorrect roll count " << hw.hw_sensor_roll_count;
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
        LOG(FATAL) << "Incorrect rollable configs for ranges " << roll_ranges << ", defaults "
                   << roll_defaults << ", radius " << roll_radius << ", or directions "
                   << roll_direction;

    } else {
        for (int i = 0; i < num_rolls; i++) {
            std::vector<std::string> range = absl::StrSplit(roll_range_tokens[i], '-');
            if (range.size() != 2) {
                LOG(FATAL) << "Incorrect rollable angle range " << roll_range_tokens[i];

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
/*

README:
typical hinge related avd configuration

hw.hw_sensor_hinge_count=2
hw.hw_sensor_hinge_defaults="180,180"
hw.hw_sensor_hinge_ranges="0-180,0-180"
hw.hw_sensor_hinge_angles_posture_definitions="0-30&0-45, 30-150&45-120, 150-180&120-180"
hw.hw_sensor_posture_list="1, 2, 3"

*/
void FoldableModel::InitFoldableHinge(const android::goldfish::HardwareConfig& hw) {
    if (!hw.hw_sensor_hinge) {
        state_.config.num_hinges = 0;
        return;
    }

    struct FoldableConfig& config = state_.config;

    auto type = static_cast<FoldableDisplayType>(hw.hw_sensor_hinge_type);
    if (type >= FoldableDisplayType::kTypeMax) {
        type = FoldableDisplayType::kHorizontalSplit;
    }
    config.type = type;

    config.fold_at_posture = static_cast<enum FoldablePostures>(
            hw.hw_sensor_hinge_fold_to_displayRegion_0_1_at_posture);

    // number
    int num_hinges = hw.hw_sensor_hinge_count;
    if (num_hinges < 0 || num_hinges > ANDROID_FOLDABLE_MAX_HINGES) {
        num_hinges = 0;
        LOG(FATAL) << "Incorrect hinge count " << hw.hw_sensor_hinge_count;
    }
    config.num_hinges = num_hinges;

    const std::string hinge_ranges(hw.hw_sensor_hinge_ranges);
    const std::string hinge_defaults(hw.hw_sensor_hinge_defaults);
    const std::string hinge_areas(hw.hw_sensor_hinge_areas);

    std::vector<std::string> range_tokens = absl::StrSplit(hinge_ranges, ',');
    std::vector<std::string> default_tokens = absl::StrSplit(hinge_defaults, ',');
    std::vector<std::string> area_tokens = absl::StrSplit(hinge_areas, ',');

    if (range_tokens.size() != static_cast<size_t>(num_hinges) ||
        default_tokens.size() != static_cast<size_t>(num_hinges) ||
        area_tokens.size() != static_cast<size_t>(num_hinges)) {
        LOG(FATAL) << "Incorrect hinge configs for ranges " << hinge_ranges << ", defaults "
                   << hinge_defaults << ", or areas " << hinge_areas;
    } else {
        for (int i = 0; i < num_hinges; i++) {
            std::vector<std::string> angles = absl::StrSplit(range_tokens[i], '-');
            std::vector<std::string> area = absl::StrSplit(area_tokens[i], '-');
            if (angles.size() != 2 || (area.size() != 2 && area.size() != 4)) {
                LOG(FATAL) << "Incorrect hinge angle range " << range_tokens[i] << " or area "
                           << area_tokens[i];
            } else {
                const float min_deg = std::stof(angles[0]);
                const float max_deg = std::stof(angles[1]);
                const float def_deg = std::stof(default_tokens[i]);

                state_.current_hinge_degrees[i] = def_deg;

                config.hinge_params[i].min_degrees = min_deg;
                config.hinge_params[i].max_degrees = max_deg;
                config.hinge_params[i].default_degrees = def_deg;
                config.hinge_params[i].display_id = 0;

                if (area.size() == 2) {
                    // percentage on screen and width config style
                    if (type == FoldableDisplayType::kHorizontalSplit) {
                        config.hinge_params[i].x = 0;
                        config.hinge_params[i].y = static_cast<int>(
                                std::stof(area[0]) * static_cast<float>(hw.hw_lcd_height) / 100.0F);
                        config.hinge_params[i].width = hw.hw_lcd_width;
                        config.hinge_params[i].height = std::stoi(area[1]);
                    } else {
                        config.hinge_params[i].x = static_cast<int>(
                                std::stof(area[0]) * static_cast<float>(hw.hw_lcd_width) / 100.0F);
                        config.hinge_params[i].y = 0;
                        config.hinge_params[i].width = std::stoi(area[1]);
                        config.hinge_params[i].height = hw.hw_lcd_height;
                    }
                } else {
                    config.hinge_params[i].x = std::stoi(area[0]);
                    config.hinge_params[i].y = std::stoi(area[1]);
                    config.hinge_params[i].width = std::stoi(area[2]);
                    config.hinge_params[i].height = std::stoi(area[3]);
                }
            }
        }
    }

    // Postures parsing
    const std::string posture_list(hw.hw_sensor_posture_list);
    const std::string posture_definitions(hw.hw_sensor_hinge_angles_posture_definitions);

    if (!posture_list.empty() && !posture_definitions.empty()) {
        std::vector<std::string> posture_tokens = absl::StrSplit(posture_list, ',');
        std::vector<std::string> def_tokens = absl::StrSplit(posture_definitions, ',');

        if (posture_tokens.size() == def_tokens.size()) {
            for (size_t i = 0; i < posture_tokens.size(); ++i) {
                AnglesToPosture atp;
                atp.posture = static_cast<FoldablePostures>(std::stoi(posture_tokens[i]));

                std::vector<std::string> hinge_defs = absl::StrSplit(def_tokens[i], '&');
                for (size_t j = 0; j < hinge_defs.size() && j < ANDROID_FOLDABLE_MAX_HINGES; ++j) {
                    std::vector<std::string> range = absl::StrSplit(hinge_defs[j], '-');
                    if (range.size() >= 2) {
                        atp.angles[j].left = std::stof(range[0]);
                        atp.angles[j].right = std::stof(range[1]);
                        if (range.size() >= 3) {
                            atp.angles[j].default_value = std::stof(range[2]);
                        } else {
                            atp.angles[j].default_value =
                                    (atp.angles[j].left + atp.angles[j].right) / 2.0F;
                        }
                    }
                }
                angles_to_postures_.push_back(atp);
            }
        }
    }

    // Determine initial posture based on default hinge angles
    state_.current_posture = FoldablePostures::kUnknown;
    for (const auto& atp : angles_to_postures_) {
        bool match = true;
        for (int i = 0; i < config.num_hinges; ++i) {
            if (state_.current_hinge_degrees[i] < atp.angles[i].left ||
                state_.current_hinge_degrees[i] > atp.angles[i].right) {
                match = false;
                break;
            }
        }
        if (match) {
            state_.current_posture = atp.posture;
            break;
        }
    }
    if (state_.current_posture == FoldablePostures::kUnknown) {
        LOG(FATAL) << "Cannot decide current posture";
    }
}

FoldableModel::FoldableModel(const android::goldfish::HardwareConfig& hw) {
    InitFoldableRoll(hw);
    InitFoldableHinge(hw);
}

void FoldableModel::SetHingeAngle(uint32_t hinge_index, float degree,
                                  PhysicalInterpolation /*mode*/) {
    if (hinge_index < ANDROID_FOLDABLE_MAX_HINGES) {
        state_.current_hinge_degrees[hinge_index] = degree;

        FoldablePostures new_posture = state_.current_posture;
        for (const auto& atp : angles_to_postures_) {
            bool match = true;
            for (int i = 0; i < state_.config.num_hinges; ++i) {
                if (state_.current_hinge_degrees[i] < atp.angles[i].left ||
                    state_.current_hinge_degrees[i] > atp.angles[i].right) {
                    match = false;
                    break;
                }
            }
            if (match) {
                new_posture = atp.posture;
                break;
            }
        }

        if (new_posture != state_.current_posture) {
            state_.current_posture = new_posture;
            posture_listener_.SetValue(state_.current_posture);
        }
    }
}

void FoldableModel::SetPosture(float posture, PhysicalInterpolation /*mode*/) {
    const auto new_posture = static_cast<FoldablePostures>(posture);
    for (const auto& atp : angles_to_postures_) {
        if (atp.posture == new_posture) {
            for (int i = 0; i < state_.config.num_hinges; ++i) {
                state_.current_hinge_degrees[i] = atp.angles[i].default_value;
            }
            break;
        }
    }

    if (new_posture != state_.current_posture) {
        state_.current_posture = new_posture;
        posture_listener_.SetValue(state_.current_posture);
    }
}

void FoldableModel::SetRollable(uint32_t /*index*/, float /*percentage*/,
                                PhysicalInterpolation /*mode*/) {
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

bool FoldableModel::IsFolded() const {
    return state_.current_posture == FoldablePostures::kClosed;
}

}  // namespace goldfish::sensors
