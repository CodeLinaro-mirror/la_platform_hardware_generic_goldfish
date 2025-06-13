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

#include "android/physics/AmbientEnvironment.h"

#include <assert.h>
#include <gtest/gtest.h>

#include "aemu/base/testing/GlmTestHelpers.h"
#include "android/base/testing/TestSystem.h"
#include "android/base/testing/TestTempDir.h"

using android::base::System;
using android::base::TestSystem;
using android::physics::AmbientEnvironment;

TEST(AmbientEnvironment, DefaultParameters) {
    TestSystem mTestSystem("/");
    mTestSystem.setLiveUnixTime(false);
    mTestSystem.setUnixTime(1);

    AmbientEnvironment ambientEnvironment;

    constexpr ParameterValueType valueTypes[] = {
            ParameterValueType::TARGET, ParameterValueType::CURRENT,
            ParameterValueType::CURRENT_NO_AMBIENT_MOTION, ParameterValueType::DEFAULT};
    for (auto valueType : valueTypes) {
        SCOPED_TRACE(testing::Message() << "valueType=" << static_cast<int>(valueType));

        EXPECT_EQ(glm::vec3(0.0f, 5.9f, -48.4f), ambientEnvironment.getMagneticField(valueType));
        EXPECT_EQ(glm::vec3(0.f, -9.81f, 0.f), ambientEnvironment.getGravity(valueType));
        EXPECT_EQ(0.f, ambientEnvironment.getTemperature(valueType));
        EXPECT_EQ(1.f, ambientEnvironment.getProximity(valueType));
        EXPECT_EQ(0.f, ambientEnvironment.getLight(valueType));
        EXPECT_EQ(0.f, ambientEnvironment.getPressure(valueType));
        EXPECT_EQ(0.f, ambientEnvironment.getHumidity(valueType));
    }
}

TEST(AmbientEnvironment, SetMagneticField) {
    TestSystem mTestSystem("/");
    mTestSystem.setLiveUnixTime(false);
    mTestSystem.setUnixTime(1);
    AmbientEnvironment ambientEnvironment;
    ambientEnvironment.setMagneticField(8.f, 11.f, 20.f, PhysicalInterpolation::STEP);
    EXPECT_EQ(glm::vec3(8.f, 11.f, 20.f), ambientEnvironment.getMagneticField());
}

TEST(AmbientEnvironment, SetGravity) {
    TestSystem mTestSystem("/");
    mTestSystem.setLiveUnixTime(false);
    mTestSystem.setUnixTime(1);
    AmbientEnvironment ambientEnvironment;
    ambientEnvironment.setGravity(glm::vec3(0.f, 1.f, 2.f), PhysicalInterpolation::STEP);
    EXPECT_EQ(glm::vec3(0.f, 1.f, 2.f), ambientEnvironment.getGravity());
}

TEST(AmbientEnvironment, SetTemperature) {
    TestSystem mTestSystem("/");
    mTestSystem.setLiveUnixTime(false);
    mTestSystem.setUnixTime(1);
    AmbientEnvironment ambientEnvironment;
    ambientEnvironment.setTemperature(27.f, PhysicalInterpolation::STEP);
    EXPECT_EQ(27.f, ambientEnvironment.getTemperature());
}

TEST(AmbientEnvironment, SetProximity) {
    TestSystem mTestSystem("/");
    mTestSystem.setLiveUnixTime(false);
    mTestSystem.setUnixTime(1);
    AmbientEnvironment ambientEnvironment;
    ambientEnvironment.setProximity(8.f, PhysicalInterpolation::STEP);
    EXPECT_EQ(8.f, ambientEnvironment.getProximity());
}

TEST(AmbientEnvironment, SetLight) {
    TestSystem mTestSystem("/");
    mTestSystem.setLiveUnixTime(false);
    mTestSystem.setUnixTime(1);
    AmbientEnvironment ambientEnvironment;
    ambientEnvironment.setLight(187.f, PhysicalInterpolation::STEP);
    EXPECT_EQ(187.f, ambientEnvironment.getLight());
}

TEST(AmbientEnvironment, SetPressure) {
    TestSystem mTestSystem("/");
    mTestSystem.setLiveUnixTime(false);
    mTestSystem.setUnixTime(1);
    AmbientEnvironment ambientEnvironment;
    ambientEnvironment.setPressure(823.f, PhysicalInterpolation::STEP);
    EXPECT_EQ(823.f, ambientEnvironment.getPressure());
}

TEST(AmbientEnvironment, SetHumidity) {
    TestSystem mTestSystem("/");
    mTestSystem.setLiveUnixTime(false);
    mTestSystem.setUnixTime(1);
    AmbientEnvironment ambientEnvironment;
    ambientEnvironment.setHumidity(0.67f, PhysicalInterpolation::STEP);
    EXPECT_EQ(0.67f, ambientEnvironment.getHumidity());
}

TEST(AmbientEnvironment, SetRgbcLight) {
    TestSystem mTestSystem("/");
    mTestSystem.setLiveUnixTime(false);
    mTestSystem.setUnixTime(1);
    AmbientEnvironment ambientEnvironment;
    const glm::vec4 kTarget = glm::vec4(100, 200, 300, 400);
    ambientEnvironment.setRgbcLight(kTarget, PhysicalInterpolation::STEP);
    EXPECT_EQ(kTarget, ambientEnvironment.getRgbcLight());
}
