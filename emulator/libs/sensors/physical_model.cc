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

#include "goldfish/sensors/physical_model.h"

#include "absl/log/log.h"

#include "android/goldfish/hardware_config.h"
#include "goldfish/physics/ambient_environment.h"
#include "goldfish/physics/body_model.h"
#include "goldfish/physics/glm_helpers.h"
#include "goldfish/physics/inertial_model.h"
#include "goldfish/sensors/foldable_model.h"

using goldfish::physics::AmbientState;
using goldfish::physics::BodyState;
using goldfish::physics::InertialState;

namespace goldfish::sensors {
namespace {
void getValues(const vec3& value, float* out, const size_t count) {
    if (count > 0) out[0] = value.x;
    if (count > 1) out[1] = value.y;
    if (count > 2) out[2] = value.z;
}

void getValues(const vec4& value, float* out, const size_t count) {
    if (count > 0) out[0] = value.x;
    if (count > 1) out[1] = value.y;
    if (count > 2) out[2] = value.z;
    if (count > 3) out[3] = value.w;
}

void getValues(const float value, float* out, const size_t count) {
    if (count > 0) out[0] = value;
}

vec3 getvec3Value(const float* val, const size_t count) {
    return vec3{count > 0 ? val[0] : 0, count > 1 ? val[1] : 0, count > 2 ? val[2] : 0};
}

vec4 getvec4Value(const float* val, const size_t count) {
    return vec4{count > 0 ? val[0] : 0, count > 1 ? val[1] : 0, count > 2 ? val[2] : 0,
                count > 3 ? val[3] : 0};
}

float getfloatValue(const float* val, const size_t count) {
    return count > 0 ? val[0] : 0;
}
}  // namespace

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

SensorData PhysicalModel::getSensorData(const AndroidSensor sensor_id) const {
    const size_t sz = getSensorValueSize(sensor_id);
    SensorData data;
    data.value.resize(sz);
    data.measurement_id = getSensorDataImpl(sensor_id, data.value.data(), sz);
    return data;
}

void PhysicalModel::setSensorValue(const AndroidSensor sensor_id, const SensorValue& val) {
    setSensorValueImpl(sensor_id, val.data(), val.size());
}

size_t PhysicalModel::getSensorValueSize(AndroidSensor sensor_id) {
#define VALUE_SIZE_float 1
#define VALUE_SIZE_vec3 3
#define VALUE_SIZE_vec4 4
#define GOLDFISH_SENSOR_DEF(X, Y, Z, V, W) \
    case AndroidSensor::X:                 \
        return VALUE_SIZE_##V;

    switch (sensor_id) {
        GOLDFISH_SENSORS_LIST
    case AndroidSensor::MAX_SENSORS:
        break;
    }

    LOG(FATAL) << "Unexpected sensor_id: " << static_cast<int>(sensor_id);

#undef GOLDFISH_SENSOR_DEF
#undef VALUE_SIZE_vec4
#undef VALUE_SIZE_vec3
#undef VALUE_SIZE_float
}

size_t PhysicalModel::getSensorDataImpl(const AndroidSensor sensor_id, float* out,
                                        const size_t count) const {
#define GOLDFISH_SENSOR_DEF(X, Y, Z, V, W)              \
    case AndroidSensor::X:                              \
        getValues(get##Z(&measurement_id), out, count); \
        return measurement_id;

    size_t measurement_id = 0;
    switch (sensor_id) {
        GOLDFISH_SENSORS_LIST
    case AndroidSensor::MAX_SENSORS:
        break;
    }

    LOG(FATAL) << "Unexpected sensor_id: " << static_cast<int>(sensor_id);

#undef GOLDFISH_SENSOR_DEF
}

void PhysicalModel::setSensorValueImpl(AndroidSensor sensor_id, const float* val,
                                       const size_t count) {
#define GOLDFISH_SENSOR_DEF(X, Y, Z, V, W)      \
    case AndroidSensor::X:                      \
        override##Z(get##V##Value(val, count)); \
        return;

    switch (sensor_id) {
        GOLDFISH_SENSORS_LIST
    case AndroidSensor::MAX_SENSORS:
        break;
    }

    LOG(FATAL) << "Unexpected sensor_id: " << static_cast<int>(sensor_id);

#undef GOLDFISH_SENSOR_DEF
}

void PhysicalModel::setPhysicalParameterValue(const PhysicalParameter parameter, const float* val,
                                              const size_t count,
                                              const PhysicalInterpolation interpolation_mode) {
#define GOLDFISH_PHYSICAL_PARAMETER_DEF(X, Y, Z, W)                  \
    case PhysicalParameter::X:                                       \
        setTarget##Z(get##W##Value(val, count), interpolation_mode); \
        return;

    switch (parameter) {
        GOLDFISH_PHYSICAL_PARAMETERS_LIST
    case PhysicalParameter::MAX_PHYSICAL_PARAMETERS:
        break;
    }

    LOG(FATAL) << "Unexpected parameter: " << static_cast<int>(parameter);

#undef GOLDFISH_PHYSICAL_PARAMETER_DEF
}

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
                                const GETTER& physicalGetter, size_t* measurement_id) const {
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
#define GOLDFISH_SENSOR_DEF(x, y, z, v, w)                              \
    void PhysicalModel::OVERRIDE_FUNCTION_NAME(z)(v override_value) {   \
        setOverride(SENSOR_NAME(x), &OVERRIDE_NAME(z), override_value); \
    }

GOLDFISH_SENSORS_LIST
#undef GOLDFISH_SENSOR_DEF

// Implement getters that respect overrides.
#define GOLDFISH_SENSOR_DEF(x, y, z, v, w)                                          \
    v PhysicalModel::GET_FUNCTION_NAME(z)(size_t* measurement_id) const {           \
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

Rotation PhysicalModel::getDeviceRotation() const {
    using physics::SkinRotation;

    size_t measurementId;
    const vec3 device_accelerometer = getAccelerometer(&measurementId);
    const glm::vec3 normalized_accelerometer = glm::normalize(device_accelerometer);

    static const std::array<std::pair<glm::vec3, SkinRotation>, 4> directions{
        std::make_pair(glm::vec3(0.0f, 1.0f, 0.0f), SkinRotation::PORTRAIT),
        std::make_pair(glm::vec3(1.0f, 0.0f, 0.0f), SkinRotation::LANDSCAPE),
        std::make_pair(glm::vec3(0.0f, -1.0f, 0.0f), SkinRotation::REVERSE_PORTRAIT),
        std::make_pair(glm::vec3(-1.0f, 0.0f, 0.0f), SkinRotation::REVERSE_LANDSCAPE)};

    auto coarse_orientation = SkinRotation::PORTRAIT;
    for (const auto& v : directions) {
        if (fabs(glm::dot(normalized_accelerometer, v.first) - 1.f) < 0.1f) {
            coarse_orientation = v.second;
            break;
        }
    }

    return {
        .rotation = coarse_orientation,
        .xAxis = device_accelerometer.x,
        .yAxis = device_accelerometer.y,
        .zAxis = device_accelerometer.z,
    };
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
