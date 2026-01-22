// Copyright (C) 2017 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "goldfish/physics/ambient_environment.h"

#include <assert.h>
#include <gtest/gtest.h>

#include "android/base/testing/TestSystem.h"
#include "android/base/testing/TestTempDir.h"
#include "glm/glm_test_helpers.h"

using android::base::System;
using android::base::TestSystem;
using goldfish::physics::AmbientEnvironment;

TEST(AmbientEnvironment, DefaultParameters) {
    TestSystem mTestSystem("/");
    mTestSystem.setLiveUnixTime(false);
    mTestSystem.setUnixTime(1);

    AmbientEnvironment ambientEnvironment;

    constexpr ParameterValueType valueTypes[] = {
        ParameterValueType::kTarget, ParameterValueType::kCurrent,
        ParameterValueType::kCurrentNoAmbientMotion, ParameterValueType::kDefault};
    for (auto valueType : valueTypes) {
        SCOPED_TRACE(testing::Message() << "valueType=" << static_cast<int>(valueType));

        EXPECT_EQ(glm::vec3(0.0f, 5.9f, -48.4f), ambientEnvironment.GetMagneticField(valueType));
        EXPECT_EQ(glm::vec3(0.f, -9.81f, 0.f), ambientEnvironment.GetGravity(valueType));
        EXPECT_EQ(0.f, ambientEnvironment.GetTemperature(valueType));
        EXPECT_EQ(1.f, ambientEnvironment.GetProximity(valueType));
        EXPECT_EQ(0.f, ambientEnvironment.GetLight(valueType));
        EXPECT_EQ(0.f, ambientEnvironment.GetPressure(valueType));
        EXPECT_EQ(0.f, ambientEnvironment.GetHumidity(valueType));
    }
}

TEST(AmbientEnvironment, SetMagneticField) {
    TestSystem mTestSystem("/");
    mTestSystem.setLiveUnixTime(false);
    mTestSystem.setUnixTime(1);
    AmbientEnvironment ambientEnvironment;
    ambientEnvironment.SetMagneticField(8.f, 11.f, 20.f, PhysicalInterpolation::kStep);
    EXPECT_EQ(glm::vec3(8.f, 11.f, 20.f), ambientEnvironment.GetMagneticField());
}

TEST(AmbientEnvironment, SetGravity) {
    TestSystem mTestSystem("/");
    mTestSystem.setLiveUnixTime(false);
    mTestSystem.setUnixTime(1);
    AmbientEnvironment ambientEnvironment;
    ambientEnvironment.SetGravity(glm::vec3(0.f, 1.f, 2.f), PhysicalInterpolation::kStep);
    EXPECT_EQ(glm::vec3(0.f, 1.f, 2.f), ambientEnvironment.GetGravity());
}

TEST(AmbientEnvironment, SetTemperature) {
    TestSystem mTestSystem("/");
    mTestSystem.setLiveUnixTime(false);
    mTestSystem.setUnixTime(1);
    AmbientEnvironment ambientEnvironment;
    ambientEnvironment.SetTemperature(27.f, PhysicalInterpolation::kStep);
    EXPECT_EQ(27.f, ambientEnvironment.GetTemperature());
}

TEST(AmbientEnvironment, SetProximity) {
    TestSystem mTestSystem("/");
    mTestSystem.setLiveUnixTime(false);
    mTestSystem.setUnixTime(1);
    AmbientEnvironment ambientEnvironment;
    ambientEnvironment.SetProximity(8.f, PhysicalInterpolation::kStep);
    EXPECT_EQ(8.f, ambientEnvironment.GetProximity());
}

TEST(AmbientEnvironment, SetLight) {
    TestSystem mTestSystem("/");
    mTestSystem.setLiveUnixTime(false);
    mTestSystem.setUnixTime(1);
    AmbientEnvironment ambientEnvironment;
    ambientEnvironment.SetLight(187.f, PhysicalInterpolation::kStep);
    EXPECT_EQ(187.f, ambientEnvironment.GetLight());
}

TEST(AmbientEnvironment, SetPressure) {
    TestSystem mTestSystem("/");
    mTestSystem.setLiveUnixTime(false);
    mTestSystem.setUnixTime(1);
    AmbientEnvironment ambientEnvironment;
    ambientEnvironment.SetPressure(823.f, PhysicalInterpolation::kStep);
    EXPECT_EQ(823.f, ambientEnvironment.GetPressure());
}

TEST(AmbientEnvironment, SetHumidity) {
    TestSystem mTestSystem("/");
    mTestSystem.setLiveUnixTime(false);
    mTestSystem.setUnixTime(1);
    AmbientEnvironment ambientEnvironment;
    ambientEnvironment.SetHumidity(0.67f, PhysicalInterpolation::kStep);
    EXPECT_EQ(0.67f, ambientEnvironment.GetHumidity());
}

TEST(AmbientEnvironment, SetRgbcLight) {
    TestSystem mTestSystem("/");
    mTestSystem.setLiveUnixTime(false);
    mTestSystem.setUnixTime(1);
    AmbientEnvironment ambientEnvironment;
    const glm::vec4 kTarget = glm::vec4(100, 200, 300, 400);
    ambientEnvironment.SetRgbcLight(kTarget, PhysicalInterpolation::kStep);
    EXPECT_EQ(kTarget, ambientEnvironment.GetRgbcLight());
}
