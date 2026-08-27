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

#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

namespace goldfish::sensors {

std::optional<std::vector<FoldableModel::ResizableConfig>> FoldableModel::ParseResizableConfigs(
        const std::string_view config_str) {
    std::vector<ResizableConfig> resizable_configs;
    if (config_str.empty()) {
        return resizable_configs;
    }

    for (const auto& config : absl::StrSplit(config_str, ',')) {
        const std::vector<std::string_view> parts = absl::StrSplit(config, '-');

        if ((parts.size() < 4) || (parts.size() > 5)) {
            return std::nullopt;
        } else {
            ResizableConfig rc;
            rc.name = absl::StripAsciiWhitespace(parts[0]);
            if (rc.name.empty()) {
                return std::nullopt;
            }

            if (!absl::SimpleAtoi(parts[1], &rc.id) || !absl::SimpleAtoi(parts[2], &rc.width) ||
                !absl::SimpleAtoi(parts[3], &rc.height)) {
                return std::nullopt;
            }
            if (parts.size() == 5) {
                if (!absl::SimpleAtoi(parts[4], &rc.dpi)) {
                    return std::nullopt;
                }
            }

            resizable_configs.push_back(std::move(rc));
        }
    }

    return resizable_configs;
}

void FoldableModel::InitFoldableRoll(const android::goldfish::HardwareConfig& hw) {
    if (!hw.hw_sensor_roll) {
        config_.num_rolls = 0;
        return;
    }

    auto type = static_cast<FoldableDisplayType>(hw.hw_sensor_hinge_type);
    if (type >= FoldableDisplayType::kTypeMax) {
        type = FoldableDisplayType::kHorizontalRoll;
    }
    config_.type = type;

    const int num_rolls = hw.hw_sensor_roll_count;
    if (num_rolls < 0 || num_rolls > kMaxRolls) {
        LOG(FATAL) << "Incorrect roll count " << hw.hw_sensor_roll_count;
    }
    config_.num_rolls = num_rolls;

    // resize at postures
    config_.resize_at_posture[0] = static_cast<enum FoldablePostures>(
            hw.hw_sensor_roll_resize_to_displayRegion_0_1_at_posture);
    config_.resize_at_posture[1] = static_cast<enum FoldablePostures>(
            hw.hw_sensor_roll_resize_to_displayRegion_0_2_at_posture);
    config_.resize_at_posture[2] = static_cast<enum FoldablePostures>(
            hw.hw_sensor_roll_resize_to_displayRegion_0_3_at_posture);

    // hinge angle ranges and defaults
    const std::string_view roll_ranges(hw.hw_sensor_roll_ranges);
    const std::string_view roll_defaults(hw.hw_sensor_roll_defaults);
    const std::string_view roll_radius(hw.hw_sensor_roll_radius);
    const std::string_view roll_direction(hw.hw_sensor_roll_direction);
    const std::vector<std::string_view> roll_range_tokens = absl::StrSplit(roll_ranges, ',');
    const std::vector<std::string_view> roll_default_tokens = absl::StrSplit(roll_defaults, ',');
    const std::vector<std::string_view> roll_radius_tokens = absl::StrSplit(roll_radius, ',');
    const std::vector<std::string_view> roll_direction_tokens = absl::StrSplit(roll_direction, ',');
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
                config_.rollable_params[i] = {
                    .display_id = 0,  // TODO: put 0 for now
                };
                if (!absl::SimpleAtof(roll_radius_tokens[i],
                                      &config_.rollable_params[i].roll_radius_as_display_percent) ||
                    !absl::SimpleAtof(range[0], &config_.rollable_params[i].min_rolled_percent) ||
                    !absl::SimpleAtof(range[1], &config_.rollable_params[i].max_rolled_percent) ||
                    !absl::SimpleAtof(roll_default_tokens[i],
                                      &config_.rollable_params[i].default_rolled_percent)) {
                    LOG(FATAL) << "Incorrect rollable configs";
                }
                if (!absl::SimpleAtoi(roll_direction_tokens[i],
                                      &config_.rollable_params[i].direction)) {
                    LOG(FATAL) << "Incorrect rollable direction " << roll_direction_tokens[i];
                }
            }
        }
    }
    for (unsigned int i = 0; std::cmp_less(i, config_.num_rolls); ++i) {
        state_.current_rolled_percent[i] = config_.rollable_params[i].default_rolled_percent;
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
        config_.num_hinges = 0;
        return;
    }

    auto type = static_cast<FoldableDisplayType>(hw.hw_sensor_hinge_type);
    if (type >= FoldableDisplayType::kTypeMax) {
        type = FoldableDisplayType::kHorizontalSplit;
    }
    config_.type = type;

    config_.fold_at_posture = static_cast<enum FoldablePostures>(
            hw.hw_sensor_hinge_fold_to_displayRegion_0_1_at_posture);

    folded_x_ = hw.hw_displayRegion_0_1_xOffset;
    folded_y_ = hw.hw_displayRegion_0_1_yOffset;
    folded_w_ = hw.hw_displayRegion_0_1_width;
    folded_h_ = hw.hw_displayRegion_0_1_height;

    const int num_hinges = hw.hw_sensor_hinge_count;
    if (num_hinges < 0 || num_hinges > kMaxHinges) {
        LOG(FATAL) << "Incorrect hinge count " << hw.hw_sensor_hinge_count;
    }
    config_.num_hinges = num_hinges;

    const std::string_view hinge_ranges(hw.hw_sensor_hinge_ranges);
    const std::string_view hinge_defaults(hw.hw_sensor_hinge_defaults);
    const std::string_view hinge_areas(hw.hw_sensor_hinge_areas);
    const std::vector<std::string_view> range_tokens = absl::StrSplit(hinge_ranges, ',');
    const std::vector<std::string_view> default_tokens = absl::StrSplit(hinge_defaults, ',');
    const std::vector<std::string_view> area_tokens = absl::StrSplit(hinge_areas, ',');

    if (range_tokens.size() != static_cast<size_t>(num_hinges) ||
        default_tokens.size() != static_cast<size_t>(num_hinges) ||
        area_tokens.size() != static_cast<size_t>(num_hinges)) {
        LOG(FATAL) << "Incorrect hinge configs for ranges " << hinge_ranges << ", defaults "
                   << hinge_defaults << ", or areas " << hinge_areas;
    } else {
        for (int i = 0; i < num_hinges; i++) {
            std::vector<std::string_view> angles = absl::StrSplit(range_tokens[i], '-');
            std::vector<std::string_view> area = absl::StrSplit(area_tokens[i], '-');
            if (angles.size() != 2 || (area.size() != 2 && area.size() != 4)) {
                LOG(FATAL) << "Incorrect hinge angle range " << range_tokens[i] << " or area "
                           << area_tokens[i];
            } else {
                float min_deg;
                float max_deg;
                float def_deg;
                if (!absl::SimpleAtof(angles[0], &min_deg) ||
                    !absl::SimpleAtof(angles[1], &max_deg) ||
                    !absl::SimpleAtof(default_tokens[i], &def_deg)) {
                    LOG(FATAL) << "Incorrect hinge configs for range " << range_tokens[i]
                               << " or default " << default_tokens[i];
                }

                state_.current_hinge_degrees[i] = def_deg;

                config_.hinge_params[i].min_degrees = min_deg;
                config_.hinge_params[i].max_degrees = max_deg;
                config_.hinge_params[i].default_degrees = def_deg;
                config_.hinge_params[i].display_id = 0;

                if (area.size() == 2) {
                    // percentage on screen and width config style
                    float area_0;
                    if (!absl::SimpleAtof(area[0], &area_0)) {
                        LOG(FATAL) << "Incorrect hinge area " << area[0];
                    }
                    if (type == FoldableDisplayType::kHorizontalSplit) {
                        config_.hinge_params[i].x = 0;
                        config_.hinge_params[i].y = static_cast<int>(
                                area_0 * static_cast<float>(hw.hw_lcd_height) / 100.0F);
                        config_.hinge_params[i].width = hw.hw_lcd_width;
                        if (!absl::SimpleAtoi(area[1], &config_.hinge_params[i].height)) {
                            LOG(FATAL) << "Incorrect hinge area height " << area[1];
                        }
                    } else {
                        config_.hinge_params[i].x = static_cast<int>(
                                area_0 * static_cast<float>(hw.hw_lcd_width) / 100.0F);
                        config_.hinge_params[i].y = 0;
                        if (!absl::SimpleAtoi(area[1], &config_.hinge_params[i].width)) {
                            LOG(FATAL) << "Incorrect hinge area width " << area[1];
                        }
                        config_.hinge_params[i].height = hw.hw_lcd_height;
                    }
                } else {
                    if (!absl::SimpleAtoi(area[0], &config_.hinge_params[i].x) ||
                        !absl::SimpleAtoi(area[1], &config_.hinge_params[i].y) ||
                        !absl::SimpleAtoi(area[2], &config_.hinge_params[i].width) ||
                        !absl::SimpleAtoi(area[3], &config_.hinge_params[i].height)) {
                        LOG(FATAL) << "Incorrect hinge area " << area_tokens[i];
                    }
                }
            }
        }
    }

    // Postures parsing
    const std::string_view posture_list(hw.hw_sensor_posture_list);
    const std::string_view posture_definitions(hw.hw_sensor_hinge_angles_posture_definitions);

    if (!posture_list.empty() && !posture_definitions.empty()) {
        const std::vector<std::string_view> posture_tokens = absl::StrSplit(posture_list, ',');
        const std::vector<std::string_view> def_tokens = absl::StrSplit(posture_definitions, ',');

        if (posture_tokens.size() == def_tokens.size()) {
            for (size_t i = 0; i < posture_tokens.size(); ++i) {
                AnglesToPosture atp;
                int posture_val;
                if (!absl::SimpleAtoi(posture_tokens[i], &posture_val)) {
                    LOG(FATAL) << "Incorrect posture " << posture_tokens[i];
                }
                atp.posture = static_cast<FoldablePostures>(posture_val);

                std::vector<std::string_view> hinge_defs = absl::StrSplit(def_tokens[i], '&');
                for (size_t j = 0; j < hinge_defs.size() && j < kMaxHinges; ++j) {
                    std::vector<std::string_view> range = absl::StrSplit(hinge_defs[j], '-');
                    if (range.size() >= 2) {
                        if (!absl::SimpleAtof(range[0], &atp.angles[j].left) ||
                            !absl::SimpleAtof(range[1], &atp.angles[j].right)) {
                            LOG(FATAL) << "Incorrect posture range " << hinge_defs[j];
                        }
                        if (range.size() >= 3) {
                            if (!absl::SimpleAtof(range[2], &atp.angles[j].default_value)) {
                                LOG(FATAL) << "Incorrect posture default " << hinge_defs[j];
                            }
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
        for (int i = 0; i < config_.num_hinges; ++i) {
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

FoldableModel::FoldableModel(const android::goldfish::HardwareConfig& hw, Private) {
    InitFoldableRoll(hw);
    InitFoldableHinge(hw);
    InitResizableConfigs(hw);
}

std::unique_ptr<FoldableModel> FoldableModel::Create(const android::goldfish::HardwareConfig& hw) {
    if (!hw.hw_sensor_hinge && !hw.hw_sensor_roll && hw.hw_resizable_configs.empty()) {
        return {};
    }
    return std::make_unique<FoldableModel>(hw, Private());
}

void FoldableModel::InitResizableConfigs(const android::goldfish::HardwareConfig& hw) {
    auto configs = ParseResizableConfigs(hw.hw_resizable_configs);
    CHECK(configs) << "Could not parse hw_resizable_configs: '" << hw.hw_resizable_configs << "'";
    resizable_configs_ = *std::move(configs);
}

void FoldableModel::SetHingeAngle(uint32_t hinge_index, float degree,
                                  PhysicalInterpolation /*mode*/) {
    if (hinge_index < kMaxHinges) {
        state_.current_hinge_degrees[hinge_index] = degree;

        FoldablePostures new_posture = state_.current_posture;
        for (const auto& atp : angles_to_postures_) {
            bool match = true;
            for (int i = 0; i < config_.num_hinges; ++i) {
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
            for (int i = 0; i < config_.num_hinges; ++i) {
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
    DCHECK(config_.num_hinges <= kMaxHinges);
    if (std::cmp_greater_equal(hinge_index, config_.num_hinges)) return 0.0F;
    return parameter_value_type == ParameterValueType::kDefault
                   ? config_.hinge_params[hinge_index].default_degrees
                   : state_.current_hinge_degrees[hinge_index];
}

float FoldableModel::GetPosture(ParameterValueType parameter_value_type) const {
    return parameter_value_type == ParameterValueType::kDefault
                   ? static_cast<float>(FoldablePostures::kUnknown)
                   : static_cast<float>(state_.current_posture);
}

float FoldableModel::GetRollable(uint32_t index, ParameterValueType parameter_value_type) const {
    DCHECK(config_.num_rolls <= kMaxRolls);
    if (std::cmp_greater_equal(index, config_.num_rolls)) return 0.0F;

    return parameter_value_type == ParameterValueType::kDefault
                   ? config_.rollable_params[index].default_rolled_percent
                   : state_.current_rolled_percent[index];
}

bool FoldableModel::GetFoldedArea(int* x, int* y, int* w, int* h) const {
    if (x) *x = folded_x_;
    if (y) *y = folded_y_;
    if (w) *w = folded_w_;
    if (h) *h = folded_h_;
    return (folded_w_ > 0 && folded_h_ > 0);
}

bool FoldableModel::IsFolded() const {
    return state_.current_posture == FoldablePostures::kClosed;
}

}  // namespace goldfish::sensors
