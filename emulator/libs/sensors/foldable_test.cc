// Copyright (C) 2026 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "goldfish/sensors/foldable.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"

#include "android/goldfish/fake_hardware_config.h"
#include "android/goldfish/hardware_config.h"

namespace goldfish::sensors {

using ::absl_testing::IsOk;
using ::testing::Not;

namespace {

auto IsNotOk() {
    return Not(IsOk());
}

android::goldfish::HardwareConfig CreateValidHingeConfig() {
    auto hw = android::goldfish::FakeHardwareConfig::GetHwConfig();
    hw.hw_lcd_width = 1200;
    hw.hw_lcd_height = 2400;
    hw.hw_sensor_hinge = true;
    hw.hw_sensor_hinge_count = 1;
    hw.hw_sensor_hinge_type = 0;  // kHorizontalSplit
    hw.hw_sensor_hinge_ranges = "0-180";
    hw.hw_sensor_hinge_defaults = "180";
    hw.hw_sensor_hinge_areas = "50-10";
    hw.hw_sensor_posture_list = "1, 2, 3";
    hw.hw_sensor_hinge_angles_posture_definitions = "0-30, 30-150, 150-180";
    return hw;
}

}  // namespace

TEST(MakeFoldableConfigTest, HingeTypeValidation) {
    auto hw = CreateValidHingeConfig();

    hw.hw_sensor_hinge_type = 0;
    {
        auto cfg_or_status = MakeFoldableConfig(hw);
        ASSERT_THAT(cfg_or_status, IsOk());
        auto cfg = *std::move(cfg_or_status);
        EXPECT_EQ(cfg.type, FoldableDisplayType::kHorizontalSplit);
    }

    hw.hw_sensor_hinge_type = 1;
    {
        auto cfg_or_status = MakeFoldableConfig(hw);
        ASSERT_THAT(cfg_or_status, IsOk());
        auto cfg = *std::move(cfg_or_status);
        EXPECT_EQ(cfg.type, FoldableDisplayType::kVerticalSplit);
    }

    hw.hw_sensor_hinge_type = 2;
    {
        auto cfg_or_status = MakeFoldableConfig(hw);
        ASSERT_THAT(cfg_or_status, IsOk());
        auto cfg = *std::move(cfg_or_status);
        EXPECT_EQ(cfg.type, FoldableDisplayType::kHorizontalRoll);
    }

    hw.hw_sensor_hinge_type = 3;
    {
        auto cfg_or_status = MakeFoldableConfig(hw);
        ASSERT_THAT(cfg_or_status, IsOk());
        auto cfg = *std::move(cfg_or_status);
        EXPECT_EQ(cfg.type, FoldableDisplayType::kVerticalRoll);
    }

    hw.hw_sensor_hinge_type = -1;
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_hinge_type = static_cast<int>(FoldableDisplayType::kTypeMax);
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_hinge_type = 100;
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());
}

TEST(MakeFoldableConfigTest, HingeCountValidation) {
    auto hw = CreateValidHingeConfig();

    hw.hw_sensor_hinge_count = -1;
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_hinge_count = 4;
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());
}

TEST(MakeFoldableConfigTest, HorizontalSplitPercentageArea) {
    auto hw = CreateValidHingeConfig();
    hw.hw_lcd_width = 1200;
    hw.hw_lcd_height = 2400;
    hw.hw_sensor_hinge_type = 0;  // kHorizontalSplit
    hw.hw_sensor_hinge_count = 1;
    hw.hw_sensor_hinge_ranges = "0-180";
    hw.hw_sensor_hinge_defaults = "90";
    hw.hw_sensor_hinge_areas = "50-10";
    {
        auto cfg_or_status = MakeFoldableConfig(hw);
        ASSERT_THAT(cfg_or_status, IsOk());
        auto cfg = *std::move(cfg_or_status);

        EXPECT_EQ(cfg.type, FoldableDisplayType::kHorizontalSplit);
        EXPECT_EQ(cfg.num_hinges, 1);
        EXPECT_EQ(cfg.hinge_params[0].display_id, 0);
        EXPECT_EQ(cfg.hinge_params[0].x, 0);
        EXPECT_EQ(cfg.hinge_params[0].y, 1200);  // 50% of 2400
        EXPECT_EQ(cfg.hinge_params[0].width, 1200);
        EXPECT_EQ(cfg.hinge_params[0].height, 10);
        EXPECT_FLOAT_EQ(cfg.hinge_params[0].min_degrees, 0.0f);
        EXPECT_FLOAT_EQ(cfg.hinge_params[0].max_degrees, 180.0f);
        EXPECT_FLOAT_EQ(cfg.hinge_params[0].default_degrees, 90.0f);
    }
}

TEST(MakeFoldableConfigTest, VerticalSplitPercentageArea) {
    auto hw = CreateValidHingeConfig();
    hw.hw_lcd_width = 1000;
    hw.hw_lcd_height = 2000;
    hw.hw_sensor_hinge_type = 1;  // kVerticalSplit
    hw.hw_sensor_hinge_count = 1;
    hw.hw_sensor_hinge_ranges = "0-360";
    hw.hw_sensor_hinge_defaults = "180";
    hw.hw_sensor_hinge_areas = "40-15";
    {
        auto cfg_or_status = MakeFoldableConfig(hw);
        ASSERT_THAT(cfg_or_status, IsOk());
        auto cfg = *std::move(cfg_or_status);

        EXPECT_EQ(cfg.type, FoldableDisplayType::kVerticalSplit);
        EXPECT_EQ(cfg.num_hinges, 1);
        EXPECT_EQ(cfg.hinge_params[0].display_id, 0);
        EXPECT_EQ(cfg.hinge_params[0].x, 400);  // 40% of 1000
        EXPECT_EQ(cfg.hinge_params[0].y, 0);
        EXPECT_EQ(cfg.hinge_params[0].width, 15);
        EXPECT_EQ(cfg.hinge_params[0].height, 2000);
        EXPECT_FLOAT_EQ(cfg.hinge_params[0].min_degrees, 0.0f);
        EXPECT_FLOAT_EQ(cfg.hinge_params[0].max_degrees, 360.0f);
        EXPECT_FLOAT_EQ(cfg.hinge_params[0].default_degrees, 180.0f);
    }
}

TEST(MakeFoldableConfigTest, FourTokensExplicitArea) {
    auto hw = CreateValidHingeConfig();
    hw.hw_sensor_hinge_count = 1;
    hw.hw_sensor_hinge_areas = "100-200-800-25";
    {
        auto cfg_or_status = MakeFoldableConfig(hw);
        ASSERT_THAT(cfg_or_status, IsOk());
        auto cfg = *std::move(cfg_or_status);

        EXPECT_EQ(cfg.hinge_params[0].x, 100);
        EXPECT_EQ(cfg.hinge_params[0].y, 200);
        EXPECT_EQ(cfg.hinge_params[0].width, 800);
        EXPECT_EQ(cfg.hinge_params[0].height, 25);
    }
}

TEST(MakeFoldableConfigTest, MultipleHinges) {
    auto hw = CreateValidHingeConfig();
    hw.hw_lcd_width = 1260;
    hw.hw_lcd_height = 2400;
    hw.hw_sensor_hinge_type = 0;
    hw.hw_sensor_hinge_count = 2;
    hw.hw_sensor_hinge_ranges = "0-360, 0-180";
    hw.hw_sensor_hinge_defaults = "180, 90";
    hw.hw_sensor_hinge_areas = "25-10, 50-15";
    hw.hw_sensor_posture_list = "1, 2, 3, 4";
    hw.hw_sensor_hinge_angles_posture_definitions =
            "0-30&0-15, 30-150&15-75, 150-330&75-165, 330-360&165-180";
    {
        auto cfg_or_status = MakeFoldableConfig(hw);
        ASSERT_THAT(cfg_or_status, IsOk());
        auto cfg = *std::move(cfg_or_status);

        EXPECT_EQ(cfg.num_hinges, 2);

        // Hinge 0
        EXPECT_EQ(cfg.hinge_params[0].x, 0);
        EXPECT_EQ(cfg.hinge_params[0].y, 600);  // 25% of 2400
        EXPECT_EQ(cfg.hinge_params[0].width, 1260);
        EXPECT_EQ(cfg.hinge_params[0].height, 10);
        EXPECT_FLOAT_EQ(cfg.hinge_params[0].min_degrees, 0.0f);
        EXPECT_FLOAT_EQ(cfg.hinge_params[0].max_degrees, 360.0f);
        EXPECT_FLOAT_EQ(cfg.hinge_params[0].default_degrees, 180.0f);

        // Hinge 1
        EXPECT_EQ(cfg.hinge_params[1].x, 0);
        EXPECT_EQ(cfg.hinge_params[1].y, 1200);  // 50% of 2400
        EXPECT_EQ(cfg.hinge_params[1].width, 1260);
        EXPECT_EQ(cfg.hinge_params[1].height, 15);
        EXPECT_FLOAT_EQ(cfg.hinge_params[1].min_degrees, 0.0f);
        EXPECT_FLOAT_EQ(cfg.hinge_params[1].max_degrees, 180.0f);
        EXPECT_FLOAT_EQ(cfg.hinge_params[1].default_degrees, 90.0f);
    }

    // 3 hinges
    hw.hw_sensor_hinge_count = 3;
    hw.hw_sensor_hinge_ranges = "0-180, 0-180, 0-180";
    hw.hw_sensor_hinge_defaults = "90, 90, 90";
    hw.hw_sensor_hinge_areas = "25-10, 50-10, 75-10";
    hw.hw_sensor_posture_list = "";
    hw.hw_sensor_hinge_angles_posture_definitions = "";
    {
        auto cfg_or_status = MakeFoldableConfig(hw);
        ASSERT_THAT(cfg_or_status, IsOk());
        auto cfg = *std::move(cfg_or_status);

        EXPECT_EQ(cfg.num_hinges, 3);
        EXPECT_EQ(cfg.hinge_params[2].y, 1800);  // 75% of 2400
    }
}

TEST(MakeFoldableConfigTest, FoldedDisplayRegion) {
    auto hw = CreateValidHingeConfig();
    hw.hw_displayRegion_0_1_xOffset = 10;
    hw.hw_displayRegion_0_1_yOffset = 20;
    hw.hw_displayRegion_0_1_width = 1080;
    hw.hw_displayRegion_0_1_height = 1200;
    hw.hw_sensor_hinge_fold_to_displayRegion_0_1_at_posture = 1;  // kClosed
    {
        auto cfg_or_status = MakeFoldableConfig(hw);
        ASSERT_THAT(cfg_or_status, IsOk());
        auto cfg = *std::move(cfg_or_status);

        EXPECT_EQ(cfg.folded_x, 10);
        EXPECT_EQ(cfg.folded_y, 20);
        EXPECT_EQ(cfg.folded_w, 1080);
        EXPECT_EQ(cfg.folded_h, 1200);
        EXPECT_EQ(cfg.fold_at_posture, FoldablePostures::kClosed);
    }
}

TEST(MakeFoldableConfigTest, PostureDefinitionsParsing) {
    auto hw = CreateValidHingeConfig();
    hw.hw_sensor_hinge_count = 2;
    hw.hw_sensor_hinge_ranges = "0-360, 0-180";
    hw.hw_sensor_hinge_defaults = "180, 90";
    hw.hw_sensor_hinge_areas = "25-10, 50-10";
    hw.hw_sensor_posture_list = "1, 2, 3, 4";
    hw.hw_sensor_hinge_angles_posture_definitions =
            "0-30&0-15, 30-150&15-75, 150-330&75-165, 330-360&165-180";
    {
        auto cfg_or_status = MakeFoldableConfig(hw);
        ASSERT_THAT(cfg_or_status, IsOk());
        auto cfg = *std::move(cfg_or_status);

        ASSERT_EQ(cfg.angles_to_postures.size(), 4);

        EXPECT_EQ(cfg.angles_to_postures[0].posture, FoldablePostures::kClosed);
        EXPECT_FLOAT_EQ(cfg.angles_to_postures[0].angles[0].left, 0.0f);
        EXPECT_FLOAT_EQ(cfg.angles_to_postures[0].angles[0].right, 30.0f);
        EXPECT_FLOAT_EQ(cfg.angles_to_postures[0].angles[0].default_value, 15.0f);
        EXPECT_FLOAT_EQ(cfg.angles_to_postures[0].angles[1].left, 0.0f);
        EXPECT_FLOAT_EQ(cfg.angles_to_postures[0].angles[1].right, 15.0f);
        EXPECT_FLOAT_EQ(cfg.angles_to_postures[0].angles[1].default_value, 7.5f);

        EXPECT_EQ(cfg.angles_to_postures[1].posture, FoldablePostures::kHalfOpened);
        EXPECT_FLOAT_EQ(cfg.angles_to_postures[1].angles[0].left, 30.0f);
        EXPECT_FLOAT_EQ(cfg.angles_to_postures[1].angles[0].right, 150.0f);
        EXPECT_FLOAT_EQ(cfg.angles_to_postures[1].angles[0].default_value, 90.0f);

        EXPECT_EQ(cfg.angles_to_postures[2].posture, FoldablePostures::kOpened);
        EXPECT_EQ(cfg.angles_to_postures[3].posture, FoldablePostures::kFlipped);
    }
}

TEST(MakeFoldableConfigTest, PostureDefinitionsExplicitDefaultAngle) {
    auto hw = CreateValidHingeConfig();
    hw.hw_sensor_hinge_count = 2;
    hw.hw_sensor_hinge_ranges = "0-360, 0-180";
    hw.hw_sensor_hinge_defaults = "180, 90";
    hw.hw_sensor_hinge_areas = "25-10, 50-10";
    hw.hw_sensor_posture_list = "1, 2";
    hw.hw_sensor_hinge_angles_posture_definitions = "0-30-5&0-15-7, 30-150-100&15-75-50";
    {
        auto cfg_or_status = MakeFoldableConfig(hw);
        ASSERT_THAT(cfg_or_status, IsOk());
        auto cfg = *std::move(cfg_or_status);

        ASSERT_EQ(cfg.angles_to_postures.size(), 2);

        EXPECT_EQ(cfg.angles_to_postures[0].posture, FoldablePostures::kClosed);
        EXPECT_FLOAT_EQ(cfg.angles_to_postures[0].angles[0].default_value, 5.0f);
        EXPECT_FLOAT_EQ(cfg.angles_to_postures[0].angles[1].default_value, 7.0f);

        EXPECT_EQ(cfg.angles_to_postures[1].posture, FoldablePostures::kHalfOpened);
        EXPECT_FLOAT_EQ(cfg.angles_to_postures[1].angles[0].default_value, 100.0f);
        EXPECT_FLOAT_EQ(cfg.angles_to_postures[1].angles[1].default_value, 50.0f);
    }
}

TEST(MakeFoldableConfigTest, PostureDefinitionsEmptyOrMismatchedCount) {
    auto hw = CreateValidHingeConfig();
    hw.hw_sensor_posture_list = "";
    hw.hw_sensor_hinge_angles_posture_definitions = "";
    {
        auto cfg_or_status = MakeFoldableConfig(hw);
        ASSERT_THAT(cfg_or_status, IsOk());
        auto cfg = *std::move(cfg_or_status);

        EXPECT_TRUE(cfg.angles_to_postures.empty());
    }

    // Mismatched count between posture list and definitions
    hw.hw_sensor_posture_list = "1, 2";
    hw.hw_sensor_hinge_angles_posture_definitions = "0-180";
    {
        auto cfg_or_status = MakeFoldableConfig(hw);
        ASSERT_THAT(cfg_or_status, IsOk());
        auto cfg = *std::move(cfg_or_status);

        EXPECT_TRUE(cfg.angles_to_postures.empty());
    }
}

TEST(MakeFoldableConfigTest, InvalidHingeRange) {
    auto hw = CreateValidHingeConfig();
    hw.hw_sensor_hinge_count = 2;
    hw.hw_sensor_hinge_defaults = "180, 90";
    hw.hw_sensor_hinge_areas = "25-10, 50-10";

    // Token count mismatch
    hw.hw_sensor_hinge_ranges = "0-180";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_hinge_count = 1;
    hw.hw_sensor_hinge_defaults = "180";
    hw.hw_sensor_hinge_areas = "50-10";

    // Format not min-max
    hw.hw_sensor_hinge_ranges = "180";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_hinge_ranges = "0-90-180";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    // Non-numeric values
    hw.hw_sensor_hinge_ranges = "abc-180";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_hinge_ranges = "0-abc";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());
}

TEST(MakeFoldableConfigTest, InvalidHingeDefaults) {
    auto hw = CreateValidHingeConfig();
    hw.hw_sensor_hinge_count = 2;
    hw.hw_sensor_hinge_ranges = "0-180, 0-180";
    hw.hw_sensor_hinge_areas = "25-10, 50-10";

    // Token count mismatch
    hw.hw_sensor_hinge_defaults = "180";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_hinge_count = 1;
    hw.hw_sensor_hinge_ranges = "0-180";
    hw.hw_sensor_hinge_areas = "50-10";

    // Non-numeric default
    hw.hw_sensor_hinge_defaults = "abc";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());
}

TEST(MakeFoldableConfigTest, InvalidHingeArea) {
    auto hw = CreateValidHingeConfig();
    hw.hw_sensor_hinge_count = 2;
    hw.hw_sensor_hinge_ranges = "0-180, 0-180";
    hw.hw_sensor_hinge_defaults = "180, 90";

    // Token count mismatch
    hw.hw_sensor_hinge_areas = "25-10";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_hinge_count = 1;
    hw.hw_sensor_hinge_ranges = "0-180";
    hw.hw_sensor_hinge_defaults = "180";

    // Unsupported token counts in area
    hw.hw_sensor_hinge_areas = "25";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_hinge_areas = "25-10-5";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_hinge_areas = "1-2-3-4-5";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    // Non-numeric 2-token area
    hw.hw_sensor_hinge_areas = "abc-10";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_hinge_areas = "25-abc";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    // Non-numeric 4-token area
    hw.hw_sensor_hinge_areas = "abc-0-100-200";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_hinge_areas = "0-abc-100-200";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_hinge_areas = "0-0-abc-200";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_hinge_areas = "0-0-100-abc";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());
}

TEST(MakeFoldableConfigTest, InvalidPostureDefinitions) {
    auto hw = CreateValidHingeConfig();

    // Non-numeric posture in posture_list
    hw.hw_sensor_posture_list = "abc";
    hw.hw_sensor_hinge_angles_posture_definitions = "0-180";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    // Non-numeric min angle in definition
    hw.hw_sensor_posture_list = "1";
    hw.hw_sensor_hinge_angles_posture_definitions = "abc-180";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    // Non-numeric max angle in definition
    hw.hw_sensor_posture_list = "1";
    hw.hw_sensor_hinge_angles_posture_definitions = "0-abc";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    // Non-numeric explicit default in definition
    hw.hw_sensor_posture_list = "1";
    hw.hw_sensor_hinge_angles_posture_definitions = "0-180-abc";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());
}

TEST(MakeFoldableConfigTest, RollableSingleRoll) {
    auto hw = CreateValidHingeConfig();
    hw.hw_sensor_roll_count = 1;
    hw.hw_sensor_roll_ranges = "0-100";
    hw.hw_sensor_roll_defaults = "50";
    hw.hw_sensor_roll_radius = "15.5";
    hw.hw_sensor_roll_direction = "1";
    hw.hw_sensor_roll_resize_to_displayRegion_0_1_at_posture = 2;  // kHalfOpened
    hw.hw_sensor_roll_resize_to_displayRegion_0_2_at_posture = 3;  // kOpened
    hw.hw_sensor_roll_resize_to_displayRegion_0_3_at_posture = 4;  // kFlipped
    {
        auto cfg_or_status = MakeFoldableConfig(hw);
        ASSERT_THAT(cfg_or_status, IsOk());
        auto cfg = *std::move(cfg_or_status);

        EXPECT_EQ(cfg.num_rolls, 1);
        EXPECT_EQ(cfg.resize_at_posture[0], FoldablePostures::kHalfOpened);
        EXPECT_EQ(cfg.resize_at_posture[1], FoldablePostures::kOpened);
        EXPECT_EQ(cfg.resize_at_posture[2], FoldablePostures::kFlipped);

        EXPECT_EQ(cfg.rollable_params[0].display_id, 0);
        EXPECT_FLOAT_EQ(cfg.rollable_params[0].roll_radius_as_display_percent, 15.5f);
        EXPECT_FLOAT_EQ(cfg.rollable_params[0].min_rolled_percent, 0.0f);
        EXPECT_FLOAT_EQ(cfg.rollable_params[0].max_rolled_percent, 100.0f);
        EXPECT_FLOAT_EQ(cfg.rollable_params[0].default_rolled_percent, 50.0f);
        EXPECT_EQ(cfg.rollable_params[0].direction, 1);
    }
}

TEST(MakeFoldableConfigTest, RollableDualRoll) {
    auto hw = CreateValidHingeConfig();
    hw.hw_sensor_roll_count = 2;
    hw.hw_sensor_roll_ranges = "0-100, 0-50";
    hw.hw_sensor_roll_defaults = "50, 25";
    hw.hw_sensor_roll_radius = "10.0, 5.0";
    hw.hw_sensor_roll_direction = "1, 0";
    {
        auto cfg_or_status = MakeFoldableConfig(hw);
        ASSERT_THAT(cfg_or_status, IsOk());
        auto cfg = *std::move(cfg_or_status);

        EXPECT_EQ(cfg.num_rolls, 2);
        EXPECT_FLOAT_EQ(cfg.rollable_params[0].roll_radius_as_display_percent, 10.0f);
        EXPECT_FLOAT_EQ(cfg.rollable_params[0].min_rolled_percent, 0.0f);
        EXPECT_FLOAT_EQ(cfg.rollable_params[0].max_rolled_percent, 100.0f);
        EXPECT_FLOAT_EQ(cfg.rollable_params[0].default_rolled_percent, 50.0f);
        EXPECT_EQ(cfg.rollable_params[0].direction, 1);

        EXPECT_FLOAT_EQ(cfg.rollable_params[1].roll_radius_as_display_percent, 5.0f);
        EXPECT_FLOAT_EQ(cfg.rollable_params[1].min_rolled_percent, 0.0f);
        EXPECT_FLOAT_EQ(cfg.rollable_params[1].max_rolled_percent, 50.0f);
        EXPECT_FLOAT_EQ(cfg.rollable_params[1].default_rolled_percent, 25.0f);
        EXPECT_EQ(cfg.rollable_params[1].direction, 0);
    }
}

TEST(MakeFoldableConfigTest, RollableInvalidConfig) {
    auto hw = CreateValidHingeConfig();

    // Roll count out of bounds
    hw.hw_sensor_roll_count = -1;
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_roll_count = 3;  // > kMaxRolls (2)
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    // Token count mismatch
    hw.hw_sensor_roll_count = 2;
    hw.hw_sensor_roll_ranges = "0-100";  // only 1 token
    hw.hw_sensor_roll_defaults = "50, 25";
    hw.hw_sensor_roll_radius = "10.0, 5.0";
    hw.hw_sensor_roll_direction = "1, 0";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_roll_ranges = "0-100, 0-50";
    hw.hw_sensor_roll_defaults = "50";  // only 1 token
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_roll_defaults = "50, 25";
    hw.hw_sensor_roll_radius = "10.0";  // only 1 token
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_roll_radius = "10.0, 5.0";
    hw.hw_sensor_roll_direction = "1";  // only 1 token
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    // Invalid format / non-numeric
    hw.hw_sensor_roll_count = 1;
    hw.hw_sensor_roll_defaults = "50";
    hw.hw_sensor_roll_radius = "10.0";
    hw.hw_sensor_roll_direction = "1";

    hw.hw_sensor_roll_ranges = "100";  // not min-max
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_roll_ranges = "0-50-100";  // 3 tokens
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_roll_ranges = "abc-100";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_roll_ranges = "0-abc";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_roll_ranges = "0-100";
    hw.hw_sensor_roll_radius = "abc";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_roll_radius = "10.0";
    hw.hw_sensor_roll_defaults = "abc";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());

    hw.hw_sensor_roll_defaults = "50";
    hw.hw_sensor_roll_direction = "abc";
    EXPECT_THAT(MakeFoldableConfig(hw), IsNotOk());
}

}  // namespace goldfish::sensors
