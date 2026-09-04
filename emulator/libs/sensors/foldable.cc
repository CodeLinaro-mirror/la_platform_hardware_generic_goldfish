/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "goldfish/sensors/foldable.h"

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

#include "android/status/status_macros.h"

namespace goldfish::sensors {
namespace {

/*
 * Typical hinge related avd configuration:
 *
 * hw.hw_sensor_hinge_count=2
 * hw.hw_sensor_hinge_defaults="180,180"
 * hw.hw_sensor_hinge_ranges="0-180,0-180"
 * hw.hw_sensor_hinge_angles_posture_definitions="0-30&0-45, 30-150&45-120, 150-180&120-180"
 * hw.hw_sensor_posture_list="1, 2, 3"
 */
absl::Status InitFoldableHinge(FoldableConfig& cfg, const android::goldfish::HardwareConfig& hw) {
    if (hw.hw_sensor_hinge_count < 0 || hw.hw_sensor_hinge_count > kMaxHinges) {
        return absl::InvalidArgumentError(
                absl::StrCat("Incorrect hinge count: ", hw.hw_sensor_hinge_count));
    }

    const size_t num_hinges = static_cast<size_t>(hw.hw_sensor_hinge_count);

    cfg.num_hinges = num_hinges;
    cfg.folded_x = hw.hw_displayRegion_0_1_xOffset;
    cfg.folded_y = hw.hw_displayRegion_0_1_yOffset;
    cfg.folded_w = hw.hw_displayRegion_0_1_width;
    cfg.folded_h = hw.hw_displayRegion_0_1_height;
    cfg.fold_at_posture =
            static_cast<FoldablePostures>(hw.hw_sensor_hinge_fold_to_displayRegion_0_1_at_posture);

    const std::string_view hinge_ranges(hw.hw_sensor_hinge_ranges);
    const std::string_view hinge_defaults(hw.hw_sensor_hinge_defaults);
    const std::string_view hinge_areas(hw.hw_sensor_hinge_areas);
    const std::vector<std::string_view> range_tokens = absl::StrSplit(hinge_ranges, ',');
    const std::vector<std::string_view> default_tokens = absl::StrSplit(hinge_defaults, ',');
    const std::vector<std::string_view> area_tokens = absl::StrSplit(hinge_areas, ',');

    if ((range_tokens.size() != num_hinges) || (default_tokens.size() != num_hinges) ||
        (area_tokens.size() != num_hinges)) {
        return absl::InvalidArgumentError(absl::StrCat(
                "Incorrect hinge configs for ranges ", hinge_ranges, ", defaults ", hinge_defaults,
                ", or areas ", hinge_areas, ", num_hinges=", num_hinges));
    }

    for (size_t i = 0; i < num_hinges; ++i) {
        cfg.hinge_params[i].display_id = 0;

        if (!absl::SimpleAtof(default_tokens[i], &cfg.hinge_params[i].default_degrees)) {
            return absl::InvalidArgumentError(
                    absl::StrCat("Can't parse def_deg: '", default_tokens[i], "'"));
        }

        const std::vector<std::string_view> angles = absl::StrSplit(range_tokens[i], '-');
        float min_deg, max_deg;
        if (angles.size() != 2) {
            return absl::InvalidArgumentError(absl::StrCat(
                    "Unexpected format in range_tokens, two elements are expected, got: '",
                    range_tokens[i], "'"));
        } else if (!absl::SimpleAtof(angles[0], &cfg.hinge_params[i].min_degrees) ||
                   !absl::SimpleAtof(angles[1], &cfg.hinge_params[i].max_degrees)) {
            return absl::InvalidArgumentError(
                    absl::StrCat("Incorrect hinge configs for range_tokens ", range_tokens[i],
                                 " or default ", default_tokens[i]));
        }

        const std::vector<std::string_view> area = absl::StrSplit(area_tokens[i], '-');
        switch (area.size()) {
        case 2: {
            float area_0;
            if (!absl::SimpleAtof(area[0], &area_0)) {
                return absl::InvalidArgumentError(absl::StrCat("Incorrect hinge area ", area[0]));
            }

            if (cfg.type == FoldableDisplayType::kHorizontalSplit) {
                cfg.hinge_params[i].x = 0;
                cfg.hinge_params[i].y =
                        static_cast<int>(area_0 * static_cast<float>(hw.hw_lcd_height) / 100.0F);
                cfg.hinge_params[i].width = hw.hw_lcd_width;
                if (!absl::SimpleAtoi(area[1], &cfg.hinge_params[i].height)) {
                    return absl::InvalidArgumentError(
                            absl::StrCat("Incorrect hinge area height ", area[1]));
                }
            } else {
                cfg.hinge_params[i].x =
                        static_cast<int>(area_0 * static_cast<float>(hw.hw_lcd_width) / 100.0F);
                cfg.hinge_params[i].y = 0;
                if (!absl::SimpleAtoi(area[1], &cfg.hinge_params[i].width)) {
                    return absl::InvalidArgumentError(
                            absl::StrCat("Incorrect hinge area width ", area[1]));
                }
                cfg.hinge_params[i].height = hw.hw_lcd_height;
            }
        } break;

        case 4:
            if (!absl::SimpleAtoi(area[0], &cfg.hinge_params[i].x) ||
                !absl::SimpleAtoi(area[1], &cfg.hinge_params[i].y) ||
                !absl::SimpleAtoi(area[2], &cfg.hinge_params[i].width) ||
                !absl::SimpleAtoi(area[3], &cfg.hinge_params[i].height)) {
                return absl::InvalidArgumentError(
                        absl::StrCat("Incorrect hinge area: '", area_tokens[i], "'"));
            }
            break;

        default:
            return absl::InvalidArgumentError(absl::StrCat(
                    "Incorrect hinge configs for area_tokens: '", area_tokens[i], "'"));
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
                int posture_val;
                if (!absl::SimpleAtoi(posture_tokens[i], &posture_val)) {
                    return absl::InvalidArgumentError(
                            absl::StrCat("Incorrect posture: '", posture_tokens[i], "'"));
                }

                AnglesToPosture atp = {
                    .posture = static_cast<FoldablePostures>(posture_val),
                };

                const std::vector<std::string_view> hinge_defs = absl::StrSplit(def_tokens[i], '&');
                for (size_t j = 0; j < hinge_defs.size() && j < kMaxHinges; ++j) {
                    const std::vector<std::string_view> range = absl::StrSplit(hinge_defs[j], '-');
                    if (range.size() >= 2) {
                        if (!absl::SimpleAtof(range[0], &atp.angles[j].left) ||
                            !absl::SimpleAtof(range[1], &atp.angles[j].right)) {
                            return absl::InvalidArgumentError(
                                    absl::StrCat("Incorrect posture range: '", hinge_defs[j], "'"));
                        }
                        if (range.size() >= 3) {
                            if (!absl::SimpleAtof(range[2], &atp.angles[j].default_value)) {
                                return absl::InvalidArgumentError(absl::StrCat(
                                        "Incorrect posture default: '", hinge_defs[j], "'"));
                            }
                        } else {
                            atp.angles[j].default_value =
                                    (atp.angles[j].left + atp.angles[j].right) / 2.0F;
                        }
                    }
                }

                cfg.angles_to_postures.push_back(atp);
            }
        }
    }

    return absl::OkStatus();
}

absl::Status InitFoldableRoll(FoldableConfig& cfg, const android::goldfish::HardwareConfig& hw) {
    if (hw.hw_sensor_roll_count < 0 || hw.hw_sensor_roll_count > kMaxRolls) {
        return absl::InvalidArgumentError(
                absl::StrCat("Incorrect roll count ", hw.hw_sensor_roll_count));
    }
    const size_t num_rolls = static_cast<size_t>(hw.hw_sensor_roll_count);

    cfg.num_rolls = num_rolls;
    if (!num_rolls) {
        return absl::OkStatus();
    }

    cfg.resize_at_posture[0] =
            static_cast<FoldablePostures>(hw.hw_sensor_roll_resize_to_displayRegion_0_1_at_posture);
    cfg.resize_at_posture[1] =
            static_cast<FoldablePostures>(hw.hw_sensor_roll_resize_to_displayRegion_0_2_at_posture);
    cfg.resize_at_posture[2] =
            static_cast<FoldablePostures>(hw.hw_sensor_roll_resize_to_displayRegion_0_3_at_posture);

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
        return absl::InvalidArgumentError(
                absl::StrCat("Incorrect rollable configs for ranges: '", roll_ranges,
                             "', defaults: '", roll_defaults, "', radius: '", roll_radius,
                             "', or directions: '", roll_direction, "', num_rolls=", num_rolls));
    }

    for (size_t i = 0; i < num_rolls; i++) {
        const std::vector<std::string_view> range = absl::StrSplit(roll_range_tokens[i], '-');
        if (range.size() != 2) {
            return absl::InvalidArgumentError(
                    absl::StrCat("Incorrect rollable angle range ", roll_range_tokens[i]));
        }

        cfg.rollable_params[i] = {
            .display_id = 0,  // TODO: put 0 for now
        };

        if (!absl::SimpleAtof(roll_radius_tokens[i],
                              &cfg.rollable_params[i].roll_radius_as_display_percent) ||
            !absl::SimpleAtof(range[0], &cfg.rollable_params[i].min_rolled_percent) ||
            !absl::SimpleAtof(range[1], &cfg.rollable_params[i].max_rolled_percent) ||
            !absl::SimpleAtof(roll_default_tokens[i],
                              &cfg.rollable_params[i].default_rolled_percent) ||
            !absl::SimpleAtoi(roll_direction_tokens[i], &cfg.rollable_params[i].direction)) {
            return absl::InvalidArgumentError(absl::StrCat("Incorrect rollable configs"));
        }
    }

    return absl::OkStatus();
}

}  // namespace

absl::StatusOr<FoldableConfig> MakeFoldableConfig(const android::goldfish::HardwareConfig& hw) {
    if ((hw.hw_sensor_hinge_type < 0) ||
        hw.hw_sensor_hinge_type >= static_cast<int>(FoldableDisplayType::kTypeMax)) {
        return absl::InvalidArgumentError(
                absl::StrCat("Unexpected hw_sensor_hinge_type: ", hw.hw_sensor_hinge_type));
    }

    FoldableConfig cfg = {
        .type = static_cast<FoldableDisplayType>(hw.hw_sensor_hinge_type),
    };

    RETURN_IF_ERROR(InitFoldableHinge(cfg, hw));
    RETURN_IF_ERROR(InitFoldableRoll(cfg, hw));
    return cfg;
}

}  // namespace goldfish::sensors
