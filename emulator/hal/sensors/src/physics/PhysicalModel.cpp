/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "android/physics/PhysicalModel.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdio>
#include <mutex>

#include "absl/log/check.h"

#include "aemu/base/async/ThreadLooper.h"
#include "aemu/base/files/PathUtils.h"
#include "aemu/base/files/StdioStream.h"
#include "aemu/base/utils/stream.h"
#include "android/base/file/file_io.h"
#include "android/base/system/System.h"
#include "android/emulation/control/sensors_agent.h"
#include "android/goldfish/config/avd.h"
#include "android/physics/FoldableModel.h"
#include "android/physics/Sensors.h"
#include "goldfish/physics/AmbientEnvironment.h"
#include "goldfish/physics/BodyModel.h"
#include "goldfish/physics/GlmHelpers.h"
#include "goldfish/physics/InertialModel.h"

using android::base::PathUtils;
using android::base::StdioStream;
using android::base::System;
using goldfish::physics::AmbientState;
using goldfish::physics::BodyState;
using goldfish::physics::InertialState;

namespace android {
namespace physics {

FoldableState PhysicalModel::getFoldableState() {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mFoldableModel.getFoldableState();
}

bool PhysicalModel::foldableIsFolded() {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mFoldableModel.isFolded();
}

bool PhysicalModel::getFoldedArea(int* x, int* y, int* w, int* h) {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mFoldableModel.getFoldedArea(x, y, w, h);
}

// bool PhysicalModel::isLoadingSnapshot = false;

android::base::EventNotificationSupport<FoldablePostures>* PhysicalModel::getPostureListener() {
    return mFoldableModel.getPostureListener();
}

static glm::vec3 toGlm(vec3 input) {
    return glm::vec3(input.x, input.y, input.z);
}

static glm::vec4 toGlm(vec4 input) {
    return glm::vec4(input.x, input.y, input.z, input.w);
}

static vec3 fromGlm(glm::vec3 input) {
    vec3 value;
    value.x = input.x;
    value.y = input.y;
    value.z = input.z;
    return value;
}

static vec4 fromGlm(glm::vec4 input) {
    vec4 value;
    value.x = input.x;
    value.y = input.y;
    value.z = input.z;
    value.w = input.w;
    return value;
}

PhysicalModel::PhysicalModel(const android::goldfish::Avd& avd) : mFoldableModel(avd) {}

void PhysicalModel::setCurrentTime(int64_t time_ns) {
    bool stateStabilized = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        mModelTimeNs = time_ns;
        const bool isInertialModelStable =
                mInertialModel.setCurrentTime(time_ns) == InertialState::STABLE;
        const bool isAmbientModelStable =
                mAmbientEnvironment.setCurrentTime(time_ns) == AmbientState::STABLE;
        const bool isBodyModelStable = mBodyModel.setCurrentTime(time_ns) == BodyState::STABLE;
        stateStabilized = (isInertialModelStable && isAmbientModelStable && isBodyModelStable &&
                           mIsPhysicalStateChanging);
    }

    if (stateStabilized) {
        physicalStateStabilized();
    }
}

void PhysicalModel::setGravity(float x, float y, float z) {
    mAmbientEnvironment.setGravity(glm::vec3(x, y, z), PhysicalInterpolation::STEP);
}

void PhysicalModel::setTargetInternalPosition(vec3 position, PhysicalInterpolation mode) {
    physicalStateChanging();
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        mInertialModel.setTargetPosition(toGlm(position), mode);
    }
    targetStateChanged();
}

void PhysicalModel::setTargetInternalVelocity(vec3 velocity, PhysicalInterpolation mode) {
    physicalStateChanging();
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        mInertialModel.setTargetVelocity(toGlm(velocity), mode);
    }
    targetStateChanged();
}

void PhysicalModel::setTargetInternalAmbientMotion(float bounds, PhysicalInterpolation mode) {
    physicalStateChanging();
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        mInertialModel.setTargetAmbientMotion(bounds, mode);
    }
    targetStateChanged();
}

void PhysicalModel::setTargetInternalRotation(vec3 rotation, PhysicalInterpolation mode) {
    physicalStateChanging();
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        mInertialModel.setTargetRotation(fromEulerAnglesXYZ(glm::radians(toGlm(rotation))), mode);
    }
    targetStateChanged();
}

void PhysicalModel::setTargetInternalMagneticField(vec3 field, PhysicalInterpolation mode) {
    physicalStateChanging();
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        mAmbientEnvironment.setMagneticField(field.x, field.y, field.z, mode);
    }
    targetStateChanged();
}

void PhysicalModel::setTargetInternalTemperature(float celsius, PhysicalInterpolation mode) {
    physicalStateChanging();
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        mAmbientEnvironment.setTemperature(celsius, mode);
    }
    targetStateChanged();
}

void PhysicalModel::setTargetInternalProximity(float centimeters, PhysicalInterpolation mode) {
    physicalStateChanging();
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        mAmbientEnvironment.setProximity(centimeters, mode);
    }
    targetStateChanged();
}

void PhysicalModel::setTargetInternalLight(float lux, PhysicalInterpolation mode) {
    physicalStateChanging();
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        mAmbientEnvironment.setLight(lux, mode);
    }
    targetStateChanged();
}

void PhysicalModel::setTargetInternalPressure(float hPa, PhysicalInterpolation mode) {
    physicalStateChanging();
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        mAmbientEnvironment.setPressure(hPa, mode);
    }
    targetStateChanged();
}

void PhysicalModel::setTargetInternalHumidity(float percentage, PhysicalInterpolation mode) {
    physicalStateChanging();
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        mAmbientEnvironment.setHumidity(percentage, mode);
    }
    targetStateChanged();
}

void PhysicalModel::setTargetInternalHingeAngle0(float degrees, PhysicalInterpolation mode) {
    physicalStateChanging();
    mFoldableModel.setHingeAngle(0, degrees, mode, mMutex);
    targetStateChanged();
}

void PhysicalModel::setTargetInternalHingeAngle1(float degrees, PhysicalInterpolation mode) {
    physicalStateChanging();
    mFoldableModel.setHingeAngle(1, degrees, mode, mMutex);
    targetStateChanged();
}

void PhysicalModel::setTargetInternalHingeAngle2(float degrees, PhysicalInterpolation mode) {
    physicalStateChanging();
    mFoldableModel.setHingeAngle(2, degrees, mode, mMutex);
    targetStateChanged();
}

void PhysicalModel::setTargetInternalPosture(float posture, PhysicalInterpolation mode) {
    physicalStateChanging();
    mFoldableModel.setPosture(posture, mode, mMutex);
    targetStateChanged();
}

void PhysicalModel::setTargetInternalRollable0(float percentage, PhysicalInterpolation mode) {
    physicalStateChanging();
    mFoldableModel.setRollable(0, percentage, mode, mMutex);
    targetStateChanged();
}

void PhysicalModel::setTargetInternalRollable1(float percentage, PhysicalInterpolation mode) {
    physicalStateChanging();
    mFoldableModel.setRollable(1, percentage, mode, mMutex);
    targetStateChanged();
}

void PhysicalModel::setTargetInternalRollable2(float percentage, PhysicalInterpolation mode) {
    physicalStateChanging();
    mFoldableModel.setRollable(2, percentage, mode, mMutex);
    targetStateChanged();
}

void PhysicalModel::setTargetInternalHeartRate(float bpm, PhysicalInterpolation mode) {
    physicalStateChanging();
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        mBodyModel.setHeartRate(bpm, mode);
    }
    targetStateChanged();
}

void PhysicalModel::setTargetInternalRgbcLight(vec4 light, PhysicalInterpolation mode) {
    physicalStateChanging();
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        mAmbientEnvironment.setRgbcLight(toGlm(light), mode);
    }
    targetStateChanged();
}

void PhysicalModel::setTargetInternalWristTilt(float value, PhysicalInterpolation mode) {
    physicalStateChanging();
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        mInertialModel.setWristTilt(value, mode);
    }
    targetStateChanged();
}

void PhysicalModel::setTargetInternalAccelerometerUncalibrated(vec3, PhysicalInterpolation) {
    physicalStateChanging();
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        // Not supported to set the uncalibrated accelerometer value.
    }
    targetStateChanged();
}

vec3 PhysicalModel::getParameterAccelerometerUncalibrated(ParameterValueType) const {
    return fromGlm(mInertialModel.getAcceleration());
}

vec3 PhysicalModel::getParameterPosition(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return fromGlm(mInertialModel.getPosition(parameterValueType));
}

vec3 PhysicalModel::getParameterVelocity(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return fromGlm(mInertialModel.getVelocity(parameterValueType));
}

float PhysicalModel::getParameterAmbientMotion(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mInertialModel.getAmbientMotion(parameterValueType);
}

vec3 PhysicalModel::getParameterRotation(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    const glm::vec3 rotationRadians =
            toEulerAnglesXYZ(mInertialModel.getRotation(parameterValueType));
    return fromGlm(glm::degrees(rotationRadians));
}

vec3 PhysicalModel::getParameterMagneticField(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return fromGlm(mAmbientEnvironment.getMagneticField(parameterValueType));
}

float PhysicalModel::getParameterTemperature(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mAmbientEnvironment.getTemperature(parameterValueType);
}

float PhysicalModel::getParameterProximity(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mAmbientEnvironment.getProximity(parameterValueType);
}

float PhysicalModel::getParameterLight(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mAmbientEnvironment.getLight(parameterValueType);
}

float PhysicalModel::getParameterPressure(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mAmbientEnvironment.getPressure(parameterValueType);
}

float PhysicalModel::getParameterHumidity(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mAmbientEnvironment.getHumidity(parameterValueType);
}

float PhysicalModel::getParameterHingeAngle0(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mFoldableModel.getHingeAngle(0, parameterValueType);
}

float PhysicalModel::getParameterHingeAngle1(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mFoldableModel.getHingeAngle(1, parameterValueType);
}

float PhysicalModel::getParameterHingeAngle2(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mFoldableModel.getHingeAngle(2, parameterValueType);
}

float PhysicalModel::getParameterPosture(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mFoldableModel.getPosture(parameterValueType);
}

float PhysicalModel::getParameterRollable0(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mFoldableModel.getRollable(0, parameterValueType);
}

float PhysicalModel::getParameterRollable1(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mFoldableModel.getRollable(1, parameterValueType);
}

float PhysicalModel::getParameterRollable2(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mFoldableModel.getRollable(2, parameterValueType);
}

float PhysicalModel::getParameterHeartRate(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mBodyModel.getHeartRate(parameterValueType);
}

vec4 PhysicalModel::getParameterRgbcLight(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return fromGlm(mAmbientEnvironment.getRgbcLight(parameterValueType));
}

float PhysicalModel::getParameterWristTilt(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mInertialModel.getWristTilt(parameterValueType);
}

#define GET_FUNCTION_NAME(x) get##x
#define OVERRIDE_FUNCTION_NAME(x) override##x
#define OVERRIDE_NAME(x) m##x##Override
#define SENSOR_NAME(x) AndroidSensor::x
#define PHYSICAL_NAME(x) getPhysical##x

// Implement sensor overrides.
#define SENSOR_(x, y, z, v, w)                                          \
    void PhysicalModel::OVERRIDE_FUNCTION_NAME(z)(v override_value) {   \
        setOverride(SENSOR_NAME(x), &OVERRIDE_NAME(z), override_value); \
    }

SENSORS_LIST
#undef SENSOR_

// Implement getters that respect overrides.
#define SENSOR_(x, y, z, v, w)                                                      \
    v PhysicalModel::GET_FUNCTION_NAME(z)(long* measurement_id) const {             \
        return getSensorValue<v>(SENSOR_NAME(x), &OVERRIDE_NAME(z),                 \
                                 std::bind(&PhysicalModel::PHYSICAL_NAME(z), this), \
                                 measurement_id);                                   \
    }

SENSORS_LIST
#undef SENSOR_

#undef PHYSICAL_NAME
#undef SENSOR_NAME
#undef OVERRIDE_NAME
#undef OVERRIDE_FUNCTION_NAME
#undef GET_FUNCTION_NAME

vec3 PhysicalModel::getPhysicalAccelerometer() const {
    // Implementation Note:
    // Gravity and magnetic vectors as observed by the device.
    // Note how we're applying the *inverse* of the transformation
    // represented by device_rotation_quat to the "absolute" coordinates
    // of the vectors.
    return fromGlm(glm::conjugate(mInertialModel.getRotation()) *
                   (mInertialModel.getAcceleration() - mAmbientEnvironment.getGravity()));
}

vec3 PhysicalModel::getPhysicalAccelerometerUncalibrated() const {
    // Same values for the calibrated and uncalibrated accelerometer
    // Bias will be added to the values on the guest side.
    return getPhysicalAccelerometer();
}

vec3 PhysicalModel::getPhysicalGyroscope() const {
    return fromGlm(glm::conjugate(mInertialModel.getRotation()) *
                   mInertialModel.getRotationalVelocity());
}

vec3 PhysicalModel::getPhysicalMagnetometer() const {
    return fromGlm(glm::conjugate(mInertialModel.getRotation()) *
                   mAmbientEnvironment.getMagneticField());
}

/* (x, y, z) == (azimuth, pitch, roll) */
vec3 PhysicalModel::getPhysicalOrientation() const {
    return fromGlm(toEulerAnglesXYZ(mInertialModel.getRotation()));
}

float PhysicalModel::getPhysicalTemperature() const {
    return mAmbientEnvironment.getTemperature();
}

float PhysicalModel::getPhysicalProximity() const {
    return mAmbientEnvironment.getProximity();
}

float PhysicalModel::getPhysicalLight() const {
    return mAmbientEnvironment.getLight();
}

float PhysicalModel::getPhysicalPressure() const {
    return mAmbientEnvironment.getPressure();
}

float PhysicalModel::getPhysicalHumidity() const {
    return mAmbientEnvironment.getHumidity();
}

vec3 PhysicalModel::getPhysicalMagnetometerUncalibrated() const {
    return fromGlm(glm::conjugate(mInertialModel.getRotation()) *
                   mAmbientEnvironment.getMagneticField());
}

vec3 PhysicalModel::getPhysicalGyroscopeUncalibrated() const {
    return fromGlm(glm::conjugate(mInertialModel.getRotation()) *
                   mInertialModel.getRotationalVelocity());
}

vec4 PhysicalModel::getPhysicalRgbcLight() const {
    return fromGlm(mAmbientEnvironment.getRgbcLight());
}

void PhysicalModel::getTransform(float* out_translation_x, float* out_translation_y,
                                 float* out_translation_z, float* out_rotation_x,
                                 float* out_rotation_y, float* out_rotation_z,
                                 int64_t* out_timestamp) {
    std::lock_guard<std::recursive_mutex> lock(mMutex);

    const vec3 position = getParameterPosition(ParameterValueType::CURRENT);
    *out_translation_x = position.x;
    *out_translation_y = position.y;
    *out_translation_z = position.z;
    const vec3 rotation = getParameterRotation(ParameterValueType::CURRENT);
    *out_rotation_x = rotation.x;
    *out_rotation_y = rotation.y;
    *out_rotation_z = rotation.z;
    *out_timestamp = mModelTimeNs;
}

float PhysicalModel::getPhysicalHingeAngle0() const {
    return mFoldableModel.getHingeAngle(0);
}

float PhysicalModel::getPhysicalHingeAngle1() const {
    return mFoldableModel.getHingeAngle(1);
}

float PhysicalModel::getPhysicalHingeAngle2() const {
    return mFoldableModel.getHingeAngle(2);
}

float PhysicalModel::getPhysicalHeartRate() const {
    return mBodyModel.getHeartRate();
}

float PhysicalModel::getPhysicalWristTilt() const {
    return mInertialModel.getWristTilt();
}

#define SET_TARGET_FUNCTION_NAME(x) setTarget##x
#define SET_TARGET_INTERNAL_FUNCTION_NAME(x) setTargetInternal##x
#define PHYSICAL_PARAMETER_ENUM(x) PHYSICAL_PARAMETER_##x
#define PHYSICAL_PARAMETER_(x, y, z, w)                                                    \
    void PhysicalModel::SET_TARGET_FUNCTION_NAME(z)(w value, PhysicalInterpolation mode) { \
        SET_TARGET_INTERNAL_FUNCTION_NAME(z)(value, mode);                                 \
    }

PHYSICAL_PARAMETERS_LIST
#undef PHYSICAL_PARAMETER_
#undef PHYSICAL_PARAMETER_ENUM
#undef SET_TARGET_INTERNAL_FUNCTION_NAME
#undef SET_TARGET_FUNCTION_NAME

void PhysicalModel::physicalStateChanging() {
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        // Note: We only call onPhysicalStateChanging if this is a transition
        // from stable to changing (i.e. don't call if we get to
        // physicalStateChanging calls in a row without a
        // physicalStateStabilized call in between).
        if (!mIsPhysicalStateChanging) {
            mIsPhysicalStateChanging = true;
        }
    }

    PhysicalModelChangeEvent event{
            .type = PhysicalModelChangeEvent::Type::PhysicalStateChanging,
            .model = this,
    };
    fireEvent(event);
}

void PhysicalModel::physicalStateStabilized() {
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        assert(mIsPhysicalStateChanging);

        // Increment all of the measurement ids because the physical state has
        // stabilized.
        for (size_t i = 0; i < static_cast<size_t>(AndroidSensor::MAX_SENSORS); i++) {
            mMeasurementId[i]++;
        }
        mIsPhysicalStateChanging = false;
    }

    PhysicalModelChangeEvent event{
            .type = PhysicalModelChangeEvent::Type::PhysicalStateStabilized,
            .model = this,
    };
    fireEvent(event);
}

void PhysicalModel::targetStateChanged() {
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        // When target state changes we reset all sensor overrides.
        for (size_t i = 0; i < static_cast<size_t>(AndroidSensor::MAX_SENSORS); ++i) {
            mUseOverride[i] = false;
        }
    }

    PhysicalModelChangeEvent event{
            .type = PhysicalModelChangeEvent::Type::TargetStateChanged,
            .model = this,
    };
    fireEvent(event);
}

}  // namespace physics
}  // namespace android
