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

#include "goldfish/sensors/foldable_model.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "absl/status/status_matchers.h"

#include "android/goldfish/fake_hardware_config.h"
#include "android/goldfish/hardware_config.h"
#include "goldfish/eventing/with_callbacks.h"
#include "goldfish/physics/physics.h"
#include "goldfish/sensors/foldable.h"

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

TEST(FoldableModelTest, CreateReturnsNullWhenSensorsDisabled) {
    auto hw = CreateValidHingeConfig();
    hw.hw_sensor_hinge = false;
    hw.hw_sensor_roll = false;

    const auto model = FoldableModel::Create(hw);
    ASSERT_THAT(model, IsOk());
    EXPECT_EQ(*model, nullptr);
}

TEST(FoldableModelTest, CreateReturnsNullOnInvalidConfig) {
    auto hw = CreateValidHingeConfig();
    hw.hw_sensor_hinge_type = 100;  // invalid hinge type
    ASSERT_THAT(FoldableModel::Create(hw), IsNotOk());
}

TEST(FoldableModelTest, InitialStateSingleHinge) {
    auto hw = CreateValidHingeConfig();
    hw.hw_sensor_hinge_defaults = "180";

    auto model_or_status = FoldableModel::Create(hw);
    ASSERT_THAT(model_or_status, IsOk());
    const auto model = *std::move(model_or_status);
    ASSERT_NE(model, nullptr);

    EXPECT_EQ(model->GetFoldableConfig().num_hinges, 1);
    EXPECT_FLOAT_EQ(model->GetHingeAngle(0, ParameterValueType::kDefault), 180.0f);
    EXPECT_FLOAT_EQ(model->GetHingeAngle(0, ParameterValueType::kCurrent), 180.0f);

    // Initial default angle is 180 degrees, which falls into 150-180 (kOpened).
    EXPECT_EQ(model->GetFoldablePosture(), FoldablePostures::kOpened);
    EXPECT_FLOAT_EQ(model->GetPosture(ParameterValueType::kCurrent),
                    static_cast<float>(FoldablePostures::kOpened));
    EXPECT_FLOAT_EQ(model->GetPosture(ParameterValueType::kDefault),
                    static_cast<float>(FoldablePostures::kUnknown));
    EXPECT_FALSE(model->IsFolded());
}

TEST(FoldableModelTest, SetHingeAngleUpdatesPosture) {
    auto hw = CreateValidHingeConfig();

    auto model_or_status = FoldableModel::Create(hw);
    ASSERT_THAT(model_or_status, IsOk());
    const auto model = *std::move(model_or_status);
    ASSERT_NE(model, nullptr);

    // Move into kHalfOpened range (30-150)
    model->SetHingeAngle(0, 90.0f, PhysicalInterpolation::kStep);
    EXPECT_FLOAT_EQ(model->GetHingeAngle(0), 90.0f);
    EXPECT_EQ(model->GetFoldablePosture(), FoldablePostures::kHalfOpened);
    EXPECT_FLOAT_EQ(model->GetPosture(), static_cast<float>(FoldablePostures::kHalfOpened));
    EXPECT_FALSE(model->IsFolded());

    // Move into kClosed range (0-30)
    model->SetHingeAngle(0, 0.0f, PhysicalInterpolation::kStep);
    EXPECT_FLOAT_EQ(model->GetHingeAngle(0), 0.0f);
    EXPECT_EQ(model->GetFoldablePosture(), FoldablePostures::kClosed);
    EXPECT_FLOAT_EQ(model->GetPosture(), static_cast<float>(FoldablePostures::kClosed));
    EXPECT_TRUE(model->IsFolded());

    // Move back to kOpened range (150-180)
    model->SetHingeAngle(0, 180.0f, PhysicalInterpolation::kStep);
    EXPECT_FLOAT_EQ(model->GetHingeAngle(0), 180.0f);
    EXPECT_EQ(model->GetFoldablePosture(), FoldablePostures::kOpened);
    EXPECT_FALSE(model->IsFolded());
}

TEST(FoldableModelTest, SetHingeAngleOutOfBoundsDoesNotCrash) {
    auto hw = CreateValidHingeConfig();

    auto model_or_status = FoldableModel::Create(hw);
    ASSERT_THAT(model_or_status, IsOk());
    const auto model = *std::move(model_or_status);
    ASSERT_NE(model, nullptr);

    EXPECT_FLOAT_EQ(model->GetHingeAngle(1), 0.0f);
    EXPECT_FLOAT_EQ(model->GetHingeAngle(100), 0.0f);

    // Set out-of-bounds hinge index >= kMaxHinges
    model->SetHingeAngle(kMaxHinges, 90.0f, PhysicalInterpolation::kStep);
    model->SetHingeAngle(100, 90.0f, PhysicalInterpolation::kStep);
}

TEST(FoldableModelTest, PostureGapPreservesPreviousPosture) {
    auto hw = CreateValidHingeConfig();
    // Define only Closed (0-30) and Opened (150-180); 31-149 is not covered
    hw.hw_sensor_posture_list = "1, 3";
    hw.hw_sensor_hinge_angles_posture_definitions = "0-30, 150-180";
    hw.hw_sensor_hinge_defaults = "180";

    auto model_or_status = FoldableModel::Create(hw);
    ASSERT_THAT(model_or_status, IsOk());
    const auto model = *std::move(model_or_status);
    ASSERT_NE(model, nullptr);

    EXPECT_EQ(model->GetFoldablePosture(), FoldablePostures::kOpened);

    // Set angle to 90 degrees (unknown posture range)
    model->SetHingeAngle(0, 90.0f, PhysicalInterpolation::kStep);
    EXPECT_FLOAT_EQ(model->GetHingeAngle(0), 90.0f);
    // Posture remains kOpened because CalcCurrentPosture returned kUnknown
    EXPECT_EQ(model->GetFoldablePosture(), FoldablePostures::kOpened);
}

TEST(FoldableModelTest, SetPostureUpdatesHingeAngles) {
    auto hw = CreateValidHingeConfig();

    auto model_or_status = FoldableModel::Create(hw);
    ASSERT_THAT(model_or_status, IsOk());
    const auto model = *std::move(model_or_status);
    ASSERT_NE(model, nullptr);

    // Set to kClosed: default angle calculated as (0 + 30) / 2 = 15
    model->SetPosture(static_cast<float>(FoldablePostures::kClosed), PhysicalInterpolation::kStep);
    EXPECT_EQ(model->GetFoldablePosture(), FoldablePostures::kClosed);
    EXPECT_FLOAT_EQ(model->GetHingeAngle(0), 15.0f);
    EXPECT_TRUE(model->IsFolded());

    // Set to kHalfOpened: default angle calculated as (30 + 150) / 2 = 90
    model->SetPosture(static_cast<float>(FoldablePostures::kHalfOpened),
                      PhysicalInterpolation::kStep);
    EXPECT_EQ(model->GetFoldablePosture(), FoldablePostures::kHalfOpened);
    EXPECT_FLOAT_EQ(model->GetHingeAngle(0), 90.0f);
    EXPECT_FALSE(model->IsFolded());

    // Set to kOpened: default angle calculated as (150 + 180) / 2 = 165
    model->SetPosture(static_cast<float>(FoldablePostures::kOpened), PhysicalInterpolation::kStep);
    EXPECT_EQ(model->GetFoldablePosture(), FoldablePostures::kOpened);
    EXPECT_FLOAT_EQ(model->GetHingeAngle(0), 165.0f);

    // Setting an unconfigured posture does nothing
    model->SetPosture(static_cast<float>(FoldablePostures::kFlipped), PhysicalInterpolation::kStep);
    EXPECT_EQ(model->GetFoldablePosture(), FoldablePostures::kOpened);
    EXPECT_FLOAT_EQ(model->GetHingeAngle(0), 165.0f);
}

TEST(FoldableModelTest, MultiHingeModel) {
    auto hw = CreateValidHingeConfig();
    hw.hw_sensor_hinge_count = 2;
    hw.hw_sensor_hinge_ranges = "0-360, 0-180";
    hw.hw_sensor_hinge_defaults = "200, 100";
    hw.hw_sensor_hinge_areas = "25-10, 50-10";
    hw.hw_sensor_posture_list = "1, 2, 3";
    hw.hw_sensor_hinge_angles_posture_definitions = "0-30&0-30, 31-179&31-89, 180-360&90-180";

    auto model_or_status = FoldableModel::Create(hw);
    ASSERT_THAT(model_or_status, IsOk());
    const auto model = *std::move(model_or_status);
    ASSERT_NE(model, nullptr);

    EXPECT_EQ(model->GetFoldableConfig().num_hinges, 2);

    // Initial angles are 200 and 100 -> falls into posture 3 (kOpened: 180-360 & 90-180)
    EXPECT_EQ(model->GetFoldablePosture(), FoldablePostures::kOpened);

    // Setting only hinge 0 to 15 degrees: hinge 1 is still 100 (matches none) -> posture unchanged
    model->SetHingeAngle(0, 15.0f, PhysicalInterpolation::kStep);
    EXPECT_EQ(model->GetFoldablePosture(), FoldablePostures::kOpened);

    // Setting hinge 1 to 15 degrees: both hinges are now 15 -> matches posture 1
    // (kClosed: 0-30 & 0-30)
    model->SetHingeAngle(1, 15.0f, PhysicalInterpolation::kStep);
    EXPECT_EQ(model->GetFoldablePosture(), FoldablePostures::kClosed);
    EXPECT_TRUE(model->IsFolded());

    // SetPosture to kHalfOpened: updates both hinge angles to their respective default values
    // Hinge 0: (31 + 179) / 2 = 105, Hinge 1: (31 + 89) / 2 = 60
    model->SetPosture(static_cast<float>(FoldablePostures::kHalfOpened),
                      PhysicalInterpolation::kStep);
    EXPECT_EQ(model->GetFoldablePosture(), FoldablePostures::kHalfOpened);
    EXPECT_FLOAT_EQ(model->GetHingeAngle(0), 105.0f);
    EXPECT_FLOAT_EQ(model->GetHingeAngle(1), 60.0f);
}

TEST(FoldableModelTest, PostureListenerNotification) {
    auto hw = CreateValidHingeConfig();
    hw.hw_sensor_hinge_defaults = "180";  // kOpened

    auto model_or_status = FoldableModel::Create(hw);
    ASSERT_THAT(model_or_status, IsOk());
    const auto model = *std::move(model_or_status);
    ASSERT_NE(model, nullptr);

    std::vector<FoldablePostures> posture_events;
    auto callback_handle = android::base::eventing::MakeScopedCallback(
            model->GetPostureListener(),
            [&posture_events](FoldablePostures p) { posture_events.push_back(p); });

    // Transition from kOpened to kClosed
    model->SetHingeAngle(0, 10.0f, PhysicalInterpolation::kStep);
    ASSERT_EQ(posture_events.size(), 1);
    EXPECT_EQ(posture_events.back(), FoldablePostures::kClosed);

    // Move angle within the same posture range (10 -> 20, both are kClosed)
    // ObservableValueTriggerOnUpdate should NOT trigger another event
    model->SetHingeAngle(0, 20.0f, PhysicalInterpolation::kStep);
    EXPECT_EQ(posture_events.size(), 1);

    // SetPosture to kHalfOpened
    model->SetPosture(static_cast<float>(FoldablePostures::kHalfOpened),
                      PhysicalInterpolation::kStep);
    ASSERT_EQ(posture_events.size(), 2);
    EXPECT_EQ(posture_events.back(), FoldablePostures::kHalfOpened);

    // Unregister callback handle and verify no further notifications
    callback_handle.reset();
    model->SetPosture(static_cast<float>(FoldablePostures::kOpened), PhysicalInterpolation::kStep);
    EXPECT_EQ(posture_events.size(), 2);
}

TEST(FoldableModelTest, FoldedArea) {
    auto hw = CreateValidHingeConfig();
    hw.hw_displayRegion_0_1_xOffset = 10;
    hw.hw_displayRegion_0_1_yOffset = 20;
    hw.hw_displayRegion_0_1_width = 1080;
    hw.hw_displayRegion_0_1_height = 1200;
    hw.hw_sensor_hinge_fold_to_displayRegion_0_1_at_posture = 1;

    int x = 67;
    int y = 42;
    int w = 14;
    int h = 99;
    {
        auto model_or_status = FoldableModel::Create(hw);
        ASSERT_THAT(model_or_status, IsOk());
        const auto model = *std::move(model_or_status);
        ASSERT_NE(model, nullptr);

        EXPECT_TRUE(model->GetFoldedArea(&x, &y, &w, &h));
        EXPECT_EQ(x, 10);
        EXPECT_EQ(y, 20);
        EXPECT_EQ(w, 1080);
        EXPECT_EQ(h, 1200);

        // Null pointers should not crash
        EXPECT_TRUE(model->GetFoldedArea(nullptr, nullptr, nullptr, nullptr));
    }

    // Zero folded dimensions return false
    hw.hw_displayRegion_0_1_width = 0;
    hw.hw_displayRegion_0_1_height = 0;
    {
        auto model_or_status = FoldableModel::Create(hw);
        ASSERT_THAT(model_or_status, IsOk());
        const auto no_folded_model = *std::move(model_or_status);
        ASSERT_NE(no_folded_model, nullptr);

        EXPECT_FALSE(no_folded_model->GetFoldedArea(&x, &y, &w, &h));
    }
}

TEST(FoldableModelTest, RollableSupport) {
    auto hw = CreateValidHingeConfig();
    hw.hw_sensor_roll = true;
    hw.hw_sensor_roll_count = 2;
    hw.hw_sensor_roll_ranges = "0-100, 0-50";
    hw.hw_sensor_roll_defaults = "50, 25";
    hw.hw_sensor_roll_radius = "10.0, 5.0";
    hw.hw_sensor_roll_direction = "1, 0";

    auto model_or_status = FoldableModel::Create(hw);
    ASSERT_THAT(model_or_status, IsOk());
    const auto model = *std::move(model_or_status);
    ASSERT_NE(model, nullptr);

    EXPECT_FLOAT_EQ(model->GetRollable(0, ParameterValueType::kDefault), 50.0f);
    EXPECT_FLOAT_EQ(model->GetRollable(0, ParameterValueType::kCurrent), 50.0f);
    EXPECT_FLOAT_EQ(model->GetRollable(1, ParameterValueType::kDefault), 25.0f);
    EXPECT_FLOAT_EQ(model->GetRollable(1, ParameterValueType::kCurrent), 25.0f);

    // Out of bounds rollable index
    EXPECT_FLOAT_EQ(model->GetRollable(2, ParameterValueType::kCurrent), 0.0f);
    EXPECT_FLOAT_EQ(model->GetRollable(100, ParameterValueType::kCurrent), 0.0f);

    // SetRollable should execute safely
    model->SetRollable(0, 75.0f, PhysicalInterpolation::kStep);
}

}  // namespace goldfish::sensors
