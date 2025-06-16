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
#include "android/physics/PhysicalModel.h"

#include <assert.h>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/vec3.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

#include "android/emulation/control/utils/CallbackEventSupport.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/fake-avd.h"
#include "android/goldfish/config/hardware_config.h"
#include "goldfish/physics/InertialModel.h"

using android::goldfish::Avd;
using android::physics::PhysicalModel;
using android::physics::PhysicalModelChangeEvent;
using goldfish::physics::kMinStateChangeTimeSeconds;
using goldfish::physics::nsToSeconds;
using goldfish::physics::secondsToNs;

static constexpr vec3 kDefaultAccelerometer = {0.f, 9.81f, 0.f};

#define EXPECT_VEC3_NEAR(e, a, d) \
    EXPECT_NEAR(e.x, a.x, d);     \
    EXPECT_NEAR(e.y, a.y, d);     \
    EXPECT_NEAR(e.z, a.z, d);

class PhysicalModelTest : public ::testing::Test {
  protected:
    void SetUp() override { model = std::make_unique<PhysicalModel>(mAvd); }

    std::unique_ptr<PhysicalModel> model;
    android::goldfish::FakeAvd mAvd;
};

TEST_F(PhysicalModelTest, DefaultInertialSensorValues) {
    model->setCurrentTime(1000000000L);
    long measurement_id;
    vec3 accelerometer = model->getAccelerometer(&measurement_id);
    EXPECT_VEC3_NEAR((vec3{0.f, 9.81f, 0.f}), accelerometer, 0.001f);

    vec3 gyro = model->getGyroscope(&measurement_id);
    EXPECT_VEC3_NEAR((vec3{0.f, 0.f, 0.f}), gyro, 0.001f);
}

TEST_F(PhysicalModelTest, ConstantMeasurementId) {
    model->setCurrentTime(1000000000L);
    long measurement_id0;
    model->getAccelerometer(&measurement_id0);

    model->setCurrentTime(2000000000L);

    long measurement_id1;
    model->getAccelerometer(&measurement_id1);

    EXPECT_EQ(measurement_id0, measurement_id1);
}

TEST_F(PhysicalModelTest, NewMeasurementId) {
    model->setCurrentTime(1000000000L);
    long measurement_id0;
    model->getAccelerometer(&measurement_id0);

    model->setCurrentTime(2000000000L);

    vec3 targetPosition;
    targetPosition.x = 2.0f;
    targetPosition.y = 3.0f;
    targetPosition.z = 4.0f;
    model->setTargetPosition(targetPosition, PhysicalInterpolation::SMOOTH);

    long measurement_id1;
    model->getAccelerometer(&measurement_id1);

    EXPECT_NE(measurement_id0, measurement_id1);
}

TEST_F(PhysicalModelTest, SetTargetPosition) {
    model->setCurrentTime(0UL);
    vec3 targetPosition;
    targetPosition.x = 2.0f;
    targetPosition.y = 3.0f;
    targetPosition.z = 4.0f;
    model->setTargetPosition(targetPosition, PhysicalInterpolation::STEP);

    model->setCurrentTime(500000000L);

    vec3 currentPosition = model->getParameterPosition(ParameterValueType::CURRENT);

    EXPECT_VEC3_NEAR(targetPosition, currentPosition, 0.0001f);
}

TEST_F(PhysicalModelTest, SetTargetRotation) {
    model->setCurrentTime(0UL);
    vec3 targetRotation;
    targetRotation.x = 45.0f;
    targetRotation.y = 10.0f;
    targetRotation.z = 4.0f;
    model->setTargetRotation(targetRotation, PhysicalInterpolation::STEP);

    model->setCurrentTime(500000000L);
    vec3 currentRotation = model->getParameterRotation(ParameterValueType::CURRENT);

    EXPECT_VEC3_NEAR(targetRotation, currentRotation, 0.0001f);
}

struct GravityTestCase {
    glm::vec3 target_rotation;
    glm::vec3 expected_acceleration;
};

const GravityTestCase gravityTestCases[] = {
        {{0.0f, 0.0f, 0.0f}, {0.0f, 9.81f, 0.0f}},    {{90.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -9.81f}},
        {{-90.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 9.81f}},  {{0.0f, 90.0f, 0.0f}, {0.0f, 9.81f, 0.0f}},
        {{0.0f, 0.0f, 90.0f}, {9.81f, 0.0f, 0.0f}},   {{0.0f, 0.0f, -90.0f}, {-9.81f, 0.0f, 0.0f}},
        {{0.0f, 0.0f, 180.0f}, {0.0f, -9.81f, 0.0f}},
};

TEST_F(PhysicalModelTest, GravityAcceleration) {
    for (const auto& testCase : gravityTestCases) {
        model->setCurrentTime(1000000000L);

        vec3 targetRotation;
        targetRotation.x = testCase.target_rotation.x;
        targetRotation.y = testCase.target_rotation.y;
        targetRotation.z = testCase.target_rotation.z;

        model->setTargetRotation(targetRotation, PhysicalInterpolation::SMOOTH);

        model->setCurrentTime(2000000000L);
        ;

        long measurement_id;
        vec3 accelerometer = model->getAccelerometer(&measurement_id);

        EXPECT_VEC3_NEAR(testCase.expected_acceleration, accelerometer, 0.01f);
    }
}

TEST_F(PhysicalModelTest, GravityOnlyAcceleration) {
    model->setCurrentTime(1000000000L);

    vec3 targetPosition;
    targetPosition.x = 2.0f;
    targetPosition.y = 3.0f;
    targetPosition.z = 4.0f;
    // at 1 second we move the target to (2, 3, 4)
    model->setTargetPosition(targetPosition, PhysicalInterpolation::SMOOTH);

    model->setCurrentTime(2000000000L);
    // at 2 seconds the target is still at (2, 3, 4);
    model->setTargetPosition(targetPosition, PhysicalInterpolation::STEP);

    long measurement_id;
    // the acceleration is expected to be close to zero at this point.
    vec3 currentAcceleration = model->getAccelerometer(&measurement_id);
    EXPECT_VEC3_NEAR(kDefaultAccelerometer, currentAcceleration, 0.01f);
}

TEST_F(PhysicalModelTest, NonInstantaneousRotation) {
    model->setCurrentTime(0L);

    vec3 startRotation;
    startRotation.x = 0.f;
    startRotation.y = 0.f;
    startRotation.z = 0.f;
    model->setTargetRotation(startRotation, PhysicalInterpolation::STEP);

    model->setCurrentTime(1000000000L);
    vec3 newRotation;
    newRotation.x = -0.5f;
    newRotation.y = 0.0f;
    newRotation.z = 0.0f;
    model->setTargetRotation(newRotation, PhysicalInterpolation::SMOOTH);

    model->setCurrentTime(1000000000L + secondsToNs(kMinStateChangeTimeSeconds / 2.f));

    long measurement_id;
    vec3 currentGyro = model->getGyroscope(&measurement_id);
    EXPECT_LE(currentGyro.x, -0.01f);
    EXPECT_NEAR(currentGyro.y, 0.0, 0.000001f);
    EXPECT_NEAR(currentGyro.z, 0.0, 0.000001f);
}

TEST_F(PhysicalModelTest, InstantaneousRotation) {
    model->setCurrentTime(0L);

    vec3 startRotation;
    startRotation.x = 0.f;
    startRotation.y = 0.f;
    startRotation.z = 0.f;
    model->setTargetRotation(startRotation, PhysicalInterpolation::STEP);

    model->setCurrentTime(1000000000L);
    vec3 newRotation;
    newRotation.x = 180.0f;
    newRotation.y = 0.0f;
    newRotation.z = 0.0f;
    model->setTargetRotation(newRotation, PhysicalInterpolation::STEP);

    long measurement_id;
    vec3 currentGyro = model->getGyroscope(&measurement_id);
    EXPECT_VEC3_NEAR((vec3{0.f, 0.f, 0.f}), currentGyro, 0.000001f);
}

TEST_F(PhysicalModelTest, OverrideAccelerometer) {
    model->setCurrentTime(0L);

    long initial_measurement_id;
    model->getAccelerometer(&initial_measurement_id);

    vec3 overrideValue;
    overrideValue.x = 1.f;
    overrideValue.y = 2.f;
    overrideValue.z = 3.f;
    model->overrideAccelerometer(overrideValue);

    long override_measurement_id;
    vec3 sensorOverriddenValue = model->getAccelerometer(&override_measurement_id);
    EXPECT_VEC3_NEAR(overrideValue, sensorOverriddenValue, 0.000001f);

    EXPECT_NE(initial_measurement_id, override_measurement_id);

    vec3 targetPosition;
    targetPosition.x = 0.f;
    targetPosition.y = 0.f;
    targetPosition.z = 0.f;
    model->setTargetPosition(targetPosition, PhysicalInterpolation::STEP);

    long physical_measurement_id;
    vec3 sensorPhysicalValue = model->getAccelerometer(&physical_measurement_id);
    EXPECT_VEC3_NEAR(kDefaultAccelerometer, sensorPhysicalValue, 0.000001f);

    EXPECT_NE(physical_measurement_id, override_measurement_id);
    EXPECT_NE(physical_measurement_id, initial_measurement_id);
}

TEST_F(PhysicalModelTest, SetRotatedIMUResults) {
    model->setCurrentTime(0UL);

    const vec3 initialRotation{45.0f, 10.0f, 4.0f};

    model->setTargetRotation(initialRotation, PhysicalInterpolation::STEP);

    const vec3 initialPosition{2.0f, 3.0f, 4.0f};
    model->setTargetPosition(initialPosition, PhysicalInterpolation::STEP);

    const glm::quat quaternionRotation = glm::toQuat(
            glm::eulerAngleXYZ(glm::radians(initialRotation.x), glm::radians(initialRotation.y),
                               glm::radians(initialRotation.z)));

    uint64_t time = 500000000UL;
    const uint64_t stepNs = 1000UL;
    const uint64_t maxConvergenceTimeNs = time + secondsToNs(5.0f);

    const vec3 targetPosition{1.0f, 2.0f, 3.0f};

    static bool targetStateChanged = false;
    static bool physicalStateChanging = false;

    auto scoped = android::emulation::control::makeScopedCallback<PhysicalModel,
                                                                  PhysicalModelChangeEvent>(
            *model, [&](PhysicalModelChangeEvent event) {
                switch (event.type) {
                    case PhysicalModelChangeEvent::Type::PhysicalStateChanging:
                        physicalStateChanging = true;
                        break;
                    case PhysicalModelChangeEvent::Type::PhysicalStateStabilized:
                        physicalStateChanging = false;
                        break;
                    case PhysicalModelChangeEvent::Type::TargetStateChanged:
                        targetStateChanged = true;
                        break;
                    default:
                        break;
                }
            });

    model->setCurrentTime(time);
    EXPECT_FALSE(physicalStateChanging);
    model->setTargetPosition(targetPosition, PhysicalInterpolation::SMOOTH);
    EXPECT_TRUE(targetStateChanged);
    EXPECT_TRUE(physicalStateChanging);
    targetStateChanged = false;

    const glm::vec3 gravity(0.f, 9.81f, 0.f);

    glm::vec3 velocity(0.f);
    glm::vec3 position(initialPosition.x, initialPosition.y, initialPosition.z);
    const float stepSeconds = nsToSeconds(stepNs);
    long prevMeasurementId = -1;
    time += stepNs / 2;

    size_t iteration = 0;
    while (physicalStateChanging) {
        SCOPED_TRACE(testing::Message() << "Iteration " << iteration);
        ++iteration;

        ASSERT_LT(time, maxConvergenceTimeNs) << "Physical state did not stabilize";
        model->setCurrentTime(time);
        long measurementId;
        const vec3 measuredAcceleration = model->getAccelerometer(&measurementId);
        ASSERT_NE(prevMeasurementId, measurementId);
        prevMeasurementId = measurementId;
        const glm::vec3 acceleration(measuredAcceleration.x, measuredAcceleration.y,
                                     measuredAcceleration.z);
        velocity += (quaternionRotation * acceleration - gravity) * stepSeconds;
        position += velocity * stepSeconds;
        time += stepNs;
    }

    const vec3 integratedPosition{position.x, position.y, position.z};

    EXPECT_VEC3_NEAR(targetPosition, integratedPosition, 0.01f);

    EXPECT_FALSE(targetStateChanged);
}

TEST_F(PhysicalModelTest, SetRotationIMUResults) {
    model->setCurrentTime(0UL);

    vec3 initialRotation{45.0f, 10.0f, 4.0f};

    model->setTargetRotation(initialRotation, PhysicalInterpolation::STEP);

    uint64_t time = 0UL;
    const uint64_t stepNs = 5000UL;
    const uint64_t maxConvergenceTimeNs = time + secondsToNs(5.0f);

    vec3 targetRotation{-10.0f, 20.0f, 45.0f};

    static bool targetStateChanged = false;
    static bool physicalStateChanging = false;
    auto scoped = android::emulation::control::makeScopedCallback<PhysicalModel,
                                                                  PhysicalModelChangeEvent>(
            *model, [&](PhysicalModelChangeEvent event) {
                switch (event.type) {
                    case PhysicalModelChangeEvent::Type::PhysicalStateChanging:
                        physicalStateChanging = true;
                        break;
                    case PhysicalModelChangeEvent::Type::PhysicalStateStabilized:
                        physicalStateChanging = false;
                        break;
                    case PhysicalModelChangeEvent::Type::TargetStateChanged:
                        targetStateChanged = true;
                        break;
                    default:
                        break;
                }
            });

    model->setTargetRotation(targetRotation, PhysicalInterpolation::SMOOTH);
    EXPECT_TRUE(targetStateChanged);
    EXPECT_TRUE(physicalStateChanging);
    targetStateChanged = false;

    glm::quat rotation = glm::toQuat(glm::eulerAngleXYZ(glm::radians(initialRotation.x),
                                                        glm::radians(initialRotation.y),
                                                        glm::radians(initialRotation.z)));
    const float stepSeconds = nsToSeconds(stepNs);
    long prevMeasurementId = -1;
    time += stepNs / 2;
    while (physicalStateChanging) {
        ASSERT_LT(time, maxConvergenceTimeNs) << "Physical state did not stabilize";
        model->setCurrentTime(time);
        long measurementId;
        const vec3 measuredGyroscope = model->getGyroscope(&measurementId);
        ASSERT_NE(prevMeasurementId, measurementId);
        prevMeasurementId = measurementId;
        const glm::vec3 deviceSpaceRotationalVelocity(measuredGyroscope.x, measuredGyroscope.y,
                                                      measuredGyroscope.z);
        const glm::vec3 rotationalVelocity = rotation * deviceSpaceRotationalVelocity;
        const glm::mat4 deltaRotationMatrix = glm::eulerAngleXYZ(
                rotationalVelocity.x * stepSeconds, rotationalVelocity.y * stepSeconds,
                rotationalVelocity.z * stepSeconds);

        rotation = glm::quat_cast(deltaRotationMatrix) * rotation;
        time += stepNs;
    }

    const glm::quat targetRotationQuat = glm::toQuat(
            glm::eulerAngleXYZ(glm::radians(targetRotation.x), glm::radians(targetRotation.y),
                               glm::radians(targetRotation.z)));

    EXPECT_NEAR(targetRotationQuat.x, rotation.x, 0.0001f);
    EXPECT_NEAR(targetRotationQuat.y, rotation.y, 0.0001f);
    EXPECT_NEAR(targetRotationQuat.z, rotation.z, 0.0001f);
    EXPECT_NEAR(targetRotationQuat.w, rotation.w, 0.0001f);

    EXPECT_FALSE(targetStateChanged);
}

TEST_F(PhysicalModelTest, MoveWhileRotating) {
    model->setCurrentTime(0UL);

    const vec3 initialRotation{45.0f, 10.0f, 4.0f};

    model->setTargetRotation(initialRotation, PhysicalInterpolation::STEP);

    const vec3 initialPosition{2.0f, 3.0f, 4.0f};
    model->setTargetPosition(initialPosition, PhysicalInterpolation::STEP);

    uint64_t time = 0UL;
    const uint64_t stepNs = 5000UL;
    const uint64_t maxConvergenceTimeNs = time + secondsToNs(5.0f);

    const vec3 targetPosition{1.0f, 2.0f, 3.0f};
    const vec3 targetRotation{-10.0f, 20.0f, 45.0f};

    static bool targetStateChanged = false;
    static bool physicalStateChanging = false;
    auto scoped = android::emulation::control::makeScopedCallback<PhysicalModel,
                                                                  PhysicalModelChangeEvent>(
            *model, [&](PhysicalModelChangeEvent event) {
                switch (event.type) {
                    case PhysicalModelChangeEvent::Type::PhysicalStateChanging:
                        physicalStateChanging = true;
                        break;
                    case PhysicalModelChangeEvent::Type::PhysicalStateStabilized:
                        physicalStateChanging = false;
                        break;
                    case PhysicalModelChangeEvent::Type::TargetStateChanged:
                        targetStateChanged = true;
                        break;
                    default:
                        break;
                }
            });

    model->setTargetRotation(targetRotation, PhysicalInterpolation::SMOOTH);
    model->setTargetPosition(targetPosition, PhysicalInterpolation::SMOOTH);
    EXPECT_TRUE(targetStateChanged);
    EXPECT_TRUE(physicalStateChanging);
    targetStateChanged = false;

    glm::quat rotation = glm::toQuat(glm::eulerAngleXYZ(glm::radians(initialRotation.x),
                                                        glm::radians(initialRotation.y),
                                                        glm::radians(initialRotation.z)));
    const glm::vec3 gravity(0.f, 9.81f, 0.f);
    glm::vec3 velocity(0.f);
    glm::vec3 position(initialPosition.x, initialPosition.y, initialPosition.z);

    const float stepSeconds = nsToSeconds(stepNs);
    long prevGyroMeasurementId = -1;
    long prevAccelMeasurementId = -1;
    time += stepNs / 2;
    while (physicalStateChanging) {
        ASSERT_LT(time, maxConvergenceTimeNs) << "Physical state did not stabilize";
        model->setCurrentTime(time);
        long gyroMeasurementId;
        const vec3 measuredGyroscope = model->getGyroscope(&gyroMeasurementId);
        ASSERT_NE(prevGyroMeasurementId, gyroMeasurementId);
        prevGyroMeasurementId = gyroMeasurementId;
        const glm::vec3 deviceSpaceRotationalVelocity(measuredGyroscope.x, measuredGyroscope.y,
                                                      measuredGyroscope.z);
        const glm::vec3 rotationalVelocity = rotation * deviceSpaceRotationalVelocity;
        const glm::mat4 deltaRotationMatrix = glm::eulerAngleXYZ(
                rotationalVelocity.x * stepSeconds, rotationalVelocity.y * stepSeconds,
                rotationalVelocity.z * stepSeconds);

        rotation = glm::quat_cast(deltaRotationMatrix) * rotation;

        long accelMeasurementId;
        const vec3 measuredAcceleration = model->getAccelerometer(&accelMeasurementId);
        EXPECT_NE(prevAccelMeasurementId, accelMeasurementId);
        prevAccelMeasurementId = accelMeasurementId;
        const glm::vec3 acceleration(measuredAcceleration.x, measuredAcceleration.y,
                                     measuredAcceleration.z);
        velocity += (rotation * acceleration - gravity) * stepSeconds;
        position += velocity * stepSeconds;

        time += stepNs;
    }

    const glm::quat targetRotationQuat = glm::toQuat(
            glm::eulerAngleXYZ(glm::radians(targetRotation.x), glm::radians(targetRotation.y),
                               glm::radians(targetRotation.z)));

    EXPECT_NEAR(targetRotationQuat.x, rotation.x, 0.0001f);
    EXPECT_NEAR(targetRotationQuat.y, rotation.y, 0.0001f);
    EXPECT_NEAR(targetRotationQuat.z, rotation.z, 0.0001f);
    EXPECT_NEAR(targetRotationQuat.w, rotation.w, 0.0001f);

    vec3 integratedPosition{position.x, position.y, position.z};

    EXPECT_VEC3_NEAR(targetPosition, integratedPosition, 0.001f);

    EXPECT_FALSE(targetStateChanged);
}

TEST_F(PhysicalModelTest, SetVelocityAndPositionWhileRotating) {
    model->setCurrentTime(0UL);

    const vec3 initialRotation{45.0f, 10.0f, 4.0f};

    model->setTargetRotation(initialRotation, PhysicalInterpolation::STEP);

    const vec3 initialPosition{2.0f, 3.0f, 4.0f};
    model->setTargetPosition(initialPosition, PhysicalInterpolation::STEP);

    vec3 intermediateVelocity{1.0f, 1.0f, 1.0f};

    uint64_t time = 0UL;
    const uint64_t stepNs = 5000UL;

    const vec3 targetPosition{1.0f, 2.0f, 3.0f};

    const vec3 intermediateRotation{100.0f, -30.0f, -10.0f};

    const vec3 targetRotation{-10.0f, 20.0f, 45.0f};

    bool targetStateChanged = false;
    bool physicalStateChanging = false;
    auto scoped = android::emulation::control::makeScopedCallback<PhysicalModel,
                                                                  PhysicalModelChangeEvent>(
            *model, [&](PhysicalModelChangeEvent event) {
                switch (event.type) {
                    case PhysicalModelChangeEvent::Type::PhysicalStateChanging:
                        physicalStateChanging = true;
                        break;
                    case PhysicalModelChangeEvent::Type::PhysicalStateStabilized:
                        physicalStateChanging = false;
                        break;
                    case PhysicalModelChangeEvent::Type::TargetStateChanged:
                        targetStateChanged = true;
                        break;
                    default:
                        break;
                }
            });

    model->setTargetRotation(intermediateRotation, PhysicalInterpolation::SMOOTH);
    model->setTargetVelocity(intermediateVelocity, PhysicalInterpolation::SMOOTH);
    EXPECT_TRUE(targetStateChanged);
    EXPECT_TRUE(physicalStateChanging);
    targetStateChanged = false;

    glm::quat rotation = glm::toQuat(glm::eulerAngleXYZ(glm::radians(initialRotation.x),
                                                        glm::radians(initialRotation.y),
                                                        glm::radians(initialRotation.z)));
    const glm::vec3 gravity(0.f, 9.81f, 0.f);
    glm::vec3 velocity(0.f);
    glm::vec3 position(initialPosition.x, initialPosition.y, initialPosition.z);

    const float stepSeconds = nsToSeconds(stepNs);
    const float maxConvergenceTimeNs = time + secondsToNs(5.0f);
    long prevGyroMeasurementId = -1;
    long prevAccelMeasurementId = -1;
    time += stepNs / 2;
    int stepsRemainingAfterStable = 10;
    while (physicalStateChanging || stepsRemainingAfterStable > 0) {
        ASSERT_LT(time, maxConvergenceTimeNs) << "Physical state did not stabilize";
        if (!physicalStateChanging) {
            stepsRemainingAfterStable--;
        }
        if (time < 500000000 && time + stepNs >= 500000000) {
            model->setTargetRotation(targetRotation, PhysicalInterpolation::SMOOTH);
            model->setTargetPosition(targetPosition, PhysicalInterpolation::SMOOTH);
            EXPECT_TRUE(targetStateChanged);
            targetStateChanged = false;
        }
        model->setCurrentTime(time);
        long gyroMeasurementId;
        const vec3 measuredGyroscope = model->getGyroscope(&gyroMeasurementId);
        if (physicalStateChanging) {
            ASSERT_NE(prevGyroMeasurementId, gyroMeasurementId);
        }
        prevGyroMeasurementId = gyroMeasurementId;
        const glm::vec3 deviceSpaceRotationalVelocity(measuredGyroscope.x, measuredGyroscope.y,
                                                      measuredGyroscope.z);
        const glm::vec3 rotationalVelocity = rotation * deviceSpaceRotationalVelocity;
        const glm::mat4 deltaRotationMatrix = glm::eulerAngleXYZ(
                rotationalVelocity.x * stepSeconds, rotationalVelocity.y * stepSeconds,
                rotationalVelocity.z * stepSeconds);

        rotation = glm::quat_cast(deltaRotationMatrix) * rotation;

        long accelMeasurementId;
        const vec3 measuredAcceleration = model->getAccelerometer(&accelMeasurementId);
        if (physicalStateChanging) {
            ASSERT_NE(prevAccelMeasurementId, accelMeasurementId);
        }
        prevAccelMeasurementId = accelMeasurementId;
        const glm::vec3 acceleration(measuredAcceleration.x, measuredAcceleration.y,
                                     measuredAcceleration.z);
        velocity += (rotation * acceleration - gravity) * stepSeconds;
        position += velocity * stepSeconds;

        time += stepNs;
    }

    const glm::quat targetRotationQuat = glm::toQuat(
            glm::eulerAngleXYZ(glm::radians(targetRotation.x), glm::radians(targetRotation.y),
                               glm::radians(targetRotation.z)));

    EXPECT_NEAR(targetRotationQuat.x, rotation.x, 0.001f);
    EXPECT_NEAR(targetRotationQuat.y, rotation.y, 0.001f);
    EXPECT_NEAR(targetRotationQuat.z, rotation.z, 0.001f);
    EXPECT_NEAR(targetRotationQuat.w, rotation.w, 0.001f);

    const vec3 integratedPosition{position.x, position.y, position.z};

    EXPECT_VEC3_NEAR(targetPosition, integratedPosition, 0.01f);

    EXPECT_FALSE(targetStateChanged);
}

TEST_F(PhysicalModelTest, DISABLED_FoldableInitialize) {
    // Foldable is not yet supported.
    using android::goldfish::HardwareConfig;

    const_cast<HardwareConfig&>(mAvd.hw()).hw_lcd_width = 1260;
    const_cast<HardwareConfig&>(mAvd.hw()).hw_lcd_height = 2400;
    const_cast<HardwareConfig&>(mAvd.hw()).hw_sensor_hinge = true;
    const_cast<HardwareConfig&>(mAvd.hw()).hw_sensor_hinge_count = 2;
    const_cast<HardwareConfig&>(mAvd.hw()).hw_sensor_hinge_type = 0;
    const_cast<HardwareConfig&>(mAvd.hw()).hw_sensor_hinge_sub_type = 1;
    const_cast<HardwareConfig&>(mAvd.hw()).hw_sensor_hinge_ranges = (char*)"0- 360, 0-180";
    const_cast<HardwareConfig&>(mAvd.hw()).hw_sensor_hinge_defaults = (char*)"180,90";
    const_cast<HardwareConfig&>(mAvd.hw()).hw_sensor_hinge_areas = (char*)"25-10, 50-10";
    const_cast<HardwareConfig&>(mAvd.hw()).hw_sensor_posture_list = (char*)"1, 2,3 ,  4";
    const_cast<HardwareConfig&>(mAvd.hw()).hw_sensor_hinge_angles_posture_definitions =
            (char*)"0-30&0-15,  30-150 & 15-75,150-330&75-165, 330-360&165-180";

    model->setCurrentTime(1000000000L);

    FoldableState ret = model->getFoldableState();
    EXPECT_EQ(180, ret.currentHingeDegrees[0]);
    EXPECT_EQ(90, ret.currentHingeDegrees[1]);
    EXPECT_EQ(2, ret.config.numHinges);
    EXPECT_EQ(FoldableDisplayType::HORIZONTAL_SPLIT, ret.config.type);
    EXPECT_EQ(0, ret.config.hingeParams[0].displayId);
    EXPECT_EQ(0, ret.config.hingeParams[0].x);
    EXPECT_EQ(600, ret.config.hingeParams[0].y);
    EXPECT_EQ(1260, ret.config.hingeParams[0].width);
    EXPECT_EQ(10, ret.config.hingeParams[0].height);
    EXPECT_EQ(0, ret.config.hingeParams[0].minDegrees);
    EXPECT_EQ(360, ret.config.hingeParams[0].maxDegrees);
    EXPECT_EQ(0, ret.config.hingeParams[1].displayId);
    EXPECT_EQ(0, ret.config.hingeParams[1].x);
    EXPECT_EQ(1200, ret.config.hingeParams[1].y);
    EXPECT_EQ(1260, ret.config.hingeParams[1].width);
    EXPECT_EQ(10, ret.config.hingeParams[1].height);
    EXPECT_EQ(0, ret.config.hingeParams[1].minDegrees);
    EXPECT_EQ(180, ret.config.hingeParams[1].maxDegrees);
    EXPECT_EQ(180, ret.config.hingeParams[0].defaultDegrees);
    EXPECT_EQ(90, ret.config.hingeParams[1].defaultDegrees);
    EXPECT_EQ(FoldablePostures::OPENED, ret.currentPosture);
}
