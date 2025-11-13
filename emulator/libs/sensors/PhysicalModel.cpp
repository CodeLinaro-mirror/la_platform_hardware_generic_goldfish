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

#include "goldfish/sensors/PhysicalModel.h"

#include "absl/log/check.h"

#include "android/goldfish/config/hardware_config.h"
#include "goldfish/sensors/FoldableModel.h"
#include "goldfish/physics/AmbientEnvironment.h"
#include "goldfish/physics/BodyModel.h"
#include "goldfish/physics/GlmHelpers.h"
#include "goldfish/physics/InertialModel.h"

using goldfish::physics::AmbientState;
using goldfish::physics::BodyState;
using goldfish::physics::InertialState;

namespace goldfish::sensors {

FoldableState PhysicalModel::getFoldableState() const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mFoldableModel.getFoldableState();
}

bool PhysicalModel::foldableIsFolded() const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mFoldableModel.isFolded();
}

bool PhysicalModel::getFoldedArea(int* x, int* y, int* w, int* h) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mFoldableModel.getFoldedArea(x, y, w, h);
}

android::base::EventNotificationSupport<FoldablePostures>* PhysicalModel::getPostureListener() {
    return mFoldableModel.getPostureListener();
}

PhysicalModel::PhysicalModel(const android::goldfish::HardwareConfig& hw) : mFoldableModel(hw) {}

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
        mInertialModel.setTargetPosition(position, mode);
    }
    targetStateChanged();
}

void PhysicalModel::setTargetInternalVelocity(vec3 velocity, PhysicalInterpolation mode) {
    physicalStateChanging();
    {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        mInertialModel.setTargetVelocity(velocity, mode);
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
        mInertialModel.setTargetRotation(fromEulerAnglesXYZ(glm::radians(rotation)), mode);
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
        mAmbientEnvironment.setRgbcLight(light, mode);
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
    return mInertialModel.getAcceleration();
}

vec3 PhysicalModel::getParameterPosition(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mInertialModel.getPosition(parameterValueType);
}

vec3 PhysicalModel::getParameterVelocity(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mInertialModel.getVelocity(parameterValueType);
}

float PhysicalModel::getParameterAmbientMotion(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mInertialModel.getAmbientMotion(parameterValueType);
}

vec3 PhysicalModel::getParameterRotation(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    const glm::vec3 rotationRadians =
            toEulerAnglesXYZ(mInertialModel.getRotation(parameterValueType));
    return glm::degrees(rotationRadians);
}

vec3 PhysicalModel::getParameterMagneticField(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mAmbientEnvironment.getMagneticField(parameterValueType);
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
    return mAmbientEnvironment.getRgbcLight(parameterValueType);
}

float PhysicalModel::getParameterWristTilt(ParameterValueType parameterValueType) const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mInertialModel.getWristTilt(parameterValueType);
}

template <class T, class GETTER>
T PhysicalModel::getSensorValue(const AndroidSensor sensor, const T* overrideMemberPointer,
                                const GETTER& physicalGetter, long* measurement_id) const {
    const size_t sensorIndex = static_cast<size_t>(sensor);

    std::lock_guard<std::recursive_mutex> lock(mMutex);
    if (mUseOverride[static_cast<size_t>(sensor)]) {
        *measurement_id = mMeasurementId[sensorIndex];
        return *overrideMemberPointer;
    } else {
        if (mIsPhysicalStateChanging) {
            mMeasurementId[sensorIndex]++;
        }
        *measurement_id = mMeasurementId[sensorIndex];
        return physicalGetter();
    }
}

#define GET_FUNCTION_NAME(x) get##x
#define OVERRIDE_FUNCTION_NAME(x) override##x
#define OVERRIDE_NAME(x) m##x##Override
#define SENSOR_NAME(x) AndroidSensor::x
#define PHYSICAL_NAME(x) getPhysical##x

// Implement sensor overrides.
#define GOLDFISH_SENSOR_DEF(x, y, z, v, w)                                          \
    void PhysicalModel::OVERRIDE_FUNCTION_NAME(z)(v override_value) {   \
        setOverride(SENSOR_NAME(x), &OVERRIDE_NAME(z), override_value); \
    }

GOLDFISH_SENSORS_LIST
#undef GOLDFISH_SENSOR_DEF

// Implement getters that respect overrides.
#define GOLDFISH_SENSOR_DEF(x, y, z, v, w)                                          \
    v PhysicalModel::GET_FUNCTION_NAME(z)(long* measurement_id) const {             \
        return getSensorValue<v>(SENSOR_NAME(x), &OVERRIDE_NAME(z),                 \
                                 std::bind(&PhysicalModel::PHYSICAL_NAME(z), this), \
                                 measurement_id);                                   \
    }

GOLDFISH_SENSORS_LIST
#undef GOLDFISH_SENSOR_DEF

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
    return glm::conjugate(mInertialModel.getRotation()) *
            (mInertialModel.getAcceleration() - mAmbientEnvironment.getGravity());
}

vec3 PhysicalModel::getPhysicalAccelerometerUncalibrated() const {
    // Same values for the calibrated and uncalibrated accelerometer
    // Bias will be added to the values on the guest side.
    return getPhysicalAccelerometer();
}

vec3 PhysicalModel::getPhysicalGyroscope() const {
    return glm::conjugate(mInertialModel.getRotation()) * mInertialModel.getRotationalVelocity();
}

vec3 PhysicalModel::getPhysicalMagnetometer() const {
    return glm::conjugate(mInertialModel.getRotation()) * mAmbientEnvironment.getMagneticField();
}

/* (x, y, z) == (azimuth, pitch, roll) */
vec3 PhysicalModel::getPhysicalOrientation() const {
    return toEulerAnglesXYZ(mInertialModel.getRotation());
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
    return glm::conjugate(mInertialModel.getRotation()) * mAmbientEnvironment.getMagneticField();
}

vec3 PhysicalModel::getPhysicalGyroscopeUncalibrated() const {
    return glm::conjugate(mInertialModel.getRotation()) * mInertialModel.getRotationalVelocity();
}

vec4 PhysicalModel::getPhysicalRgbcLight() const {
    return mAmbientEnvironment.getRgbcLight();
}

void PhysicalModel::getTransform(float* out_translation_x, float* out_translation_y,
                                 float* out_translation_z, float* out_rotation_x,
                                 float* out_rotation_y, float* out_rotation_z,
                                 int64_t* out_timestamp) const {
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
#define GOLDFISH_PHYSICAL_PARAMETER_DEF(x, y, z, w)                                        \
    void PhysicalModel::SET_TARGET_FUNCTION_NAME(z)(w value, PhysicalInterpolation mode) { \
        SET_TARGET_INTERNAL_FUNCTION_NAME(z)(value, mode);                                 \
    }

GOLDFISH_PHYSICAL_PARAMETERS_LIST
#undef GOLDFISH_PHYSICAL_PARAMETER_DEF
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
        for (size_t i = 0; i < kNumSensors; i++) {
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
        for (size_t i = 0; i < kNumSensors; ++i) {
            mUseOverride[i] = false;
        }
    }

    PhysicalModelChangeEvent event{
            .type = PhysicalModelChangeEvent::Type::TargetStateChanged,
            .model = this,
    };
    fireEvent(event);
}

}  // namespace goldfish::sensors
