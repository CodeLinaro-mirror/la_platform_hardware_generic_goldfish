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

FoldableState PhysicalModel::GetFoldableState() const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return foldable_model_.GetFoldableState();
}

bool PhysicalModel::FoldableIsFolded() const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return goldfish::sensors::FoldableModel::IsFolded();
}

bool PhysicalModel::GetFoldedArea(int* x, int* y, int* w, int* h) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return goldfish::sensors::FoldableModel::GetFoldedArea(x, y, w, h);
}

android::base::EventNotificationSupport<FoldablePostures>* PhysicalModel::GetPostureListener() {
    return foldable_model_.GetPostureListener();
}

PhysicalModel::PhysicalModel(const android::goldfish::HardwareConfig& hw) : foldable_model_(hw) {}

SensorData PhysicalModel::GetSensorData(const AndroidSensor sensor_id) const {
    const size_t sz = GetSensorValueSize(sensor_id);
    SensorData data;
    data.value.resize(sz);
    data.measurement_id = GetSensorDataImpl(sensor_id, data.value.data(), sz);
    return data;
}

void PhysicalModel::SetSensorValue(const AndroidSensor sensor_id, const SensorValue& val) {
    SetSensorValueImpl(sensor_id, val.data(), val.size());
}

size_t PhysicalModel::GetSensorValueSize(AndroidSensor sensor_id) {
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

size_t PhysicalModel::GetSensorDataImpl(const AndroidSensor sensor_id, float* out,
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

void PhysicalModel::SetSensorValueImpl(AndroidSensor sensor_id, const float* val,
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

void PhysicalModel::SetPhysicalParameterValue(const PhysicalParameter parameter, const float* val,
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

void PhysicalModel::SetCurrentTime(int64_t time_ns) {
    bool state_stabilized = false;
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        model_time_ns_ = time_ns;
        const bool is_inertial_model_stable =
                inertial_model_.SetCurrentTime(time_ns) == InertialState::kStable;
        const bool is_ambient_model_stable = goldfish::physics::AmbientEnvironment::SetCurrentTime(
                                                     time_ns) == AmbientState::kStable;
        const bool is_body_model_stable =
                goldfish::physics::BodyModel::SetCurrentTime(time_ns) == BodyState::kStable;
        state_stabilized = (is_inertial_model_stable && is_ambient_model_stable &&
                            is_body_model_stable && is_physical_state_changing_);
    }

    if (state_stabilized) {
        PhysicalStateStabilized();
    }
}

void PhysicalModel::SetGravity(float x, float y, float z) {
    PhysicalStateChanging();
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        ambient_environment_.SetGravity(glm::vec3(x, y, z), PhysicalInterpolation::kStep);
    }
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalPosition(vec3 position, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        inertial_model_.SetTargetPosition(position, mode);
    }
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalVelocity(vec3 velocity, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        inertial_model_.SetTargetVelocity(velocity, mode);
    }
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalAmbientMotion(float bounds, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        inertial_model_.SetTarGetAmbientMotion(bounds, mode);
    }
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalRotation(vec3 rotation, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        inertial_model_.SetTargetRotation(FromEulerAnglesXyz(glm::radians(rotation)), mode);
    }
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalMagneticField(vec3 field, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        ambient_environment_.SetMagneticField(field.x, field.y, field.z, mode);
    }
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalTemperature(float celsius, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        ambient_environment_.SetTemperature(celsius, mode);
    }
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalProximity(float centimeters, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        ambient_environment_.SetProximity(centimeters, mode);
    }
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalLight(float lux, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        ambient_environment_.SetLight(lux, mode);
    }
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalPressure(float h_pa, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        ambient_environment_.SetPressure(h_pa, mode);
    }
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalHumidity(float percentage, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        ambient_environment_.SetHumidity(percentage, mode);
    }
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalHingeAngle0(float degrees, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    goldfish::sensors::FoldableModel::SetHingeAngle(0, degrees, mode, mutex_);
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalHingeAngle1(float degrees, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    goldfish::sensors::FoldableModel::SetHingeAngle(1, degrees, mode, mutex_);
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalHingeAngle2(float degrees, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    goldfish::sensors::FoldableModel::SetHingeAngle(2, degrees, mode, mutex_);
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalPosture(float posture, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    goldfish::sensors::FoldableModel::SetPosture(posture, mode, mutex_);
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalRollable0(float percentage, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    goldfish::sensors::FoldableModel::SetRollable(0, percentage, mode, mutex_);
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalRollable1(float percentage, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    goldfish::sensors::FoldableModel::SetRollable(1, percentage, mode, mutex_);
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalRollable2(float percentage, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    goldfish::sensors::FoldableModel::SetRollable(2, percentage, mode, mutex_);
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalHeartRate(float bpm, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        body_model_.SetHeartRate(bpm, mode);
    }
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalRgbcLight(vec4 light, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        ambient_environment_.SetRgbcLight(light, mode);
    }
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalWristTilt(float value, PhysicalInterpolation mode) {
    PhysicalStateChanging();
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        inertial_model_.SetWristTilt(value, mode);
    }
    TargetStateChanged();
}

void PhysicalModel::setTargetInternalAccelerometerUncalibrated(vec3, PhysicalInterpolation) {
    PhysicalStateChanging();
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        // Not supported to set the uncalibrated accelerometer value.
    }
    TargetStateChanged();
}

vec3 PhysicalModel::getParameterAccelerometerUncalibrated(ParameterValueType) const {
    return inertial_model_.GetAcceleration();
}

vec3 PhysicalModel::getParameterPosition(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return inertial_model_.GetPosition(parameter_value_type);
}

vec3 PhysicalModel::getParameterVelocity(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return inertial_model_.GetVelocity(parameter_value_type);
}

float PhysicalModel::getParameterAmbientMotion(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return inertial_model_.GetAmbientMotion(parameter_value_type);
}

vec3 PhysicalModel::getParameterRotation(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    const glm::vec3 rotation_radians =
            ToEulerAnglesXyz(inertial_model_.GetRotation(parameter_value_type));
    return glm::degrees(rotation_radians);
}

vec3 PhysicalModel::getParameterMagneticField(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return ambient_environment_.GetMagneticField(parameter_value_type);
}

float PhysicalModel::getParameterTemperature(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return ambient_environment_.GetTemperature(parameter_value_type);
}

float PhysicalModel::getParameterProximity(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return ambient_environment_.GetProximity(parameter_value_type);
}

float PhysicalModel::getParameterLight(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return ambient_environment_.GetLight(parameter_value_type);
}

float PhysicalModel::getParameterPressure(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return ambient_environment_.GetPressure(parameter_value_type);
}

float PhysicalModel::getParameterHumidity(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return ambient_environment_.GetHumidity(parameter_value_type);
}

float PhysicalModel::getParameterHingeAngle0(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return foldable_model_.GetHingeAngle(0, parameter_value_type);
}

float PhysicalModel::getParameterHingeAngle1(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return foldable_model_.GetHingeAngle(1, parameter_value_type);
}

float PhysicalModel::getParameterHingeAngle2(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return foldable_model_.GetHingeAngle(2, parameter_value_type);
}

float PhysicalModel::getParameterPosture(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return foldable_model_.GetPosture(parameter_value_type);
}

float PhysicalModel::getParameterRollable0(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return foldable_model_.GetRollable(0, parameter_value_type);
}

float PhysicalModel::getParameterRollable1(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return foldable_model_.GetRollable(1, parameter_value_type);
}

float PhysicalModel::getParameterRollable2(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return foldable_model_.GetRollable(2, parameter_value_type);
}

float PhysicalModel::getParameterHeartRate(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return body_model_.GetHeartRate(parameter_value_type);
}

vec4 PhysicalModel::getParameterRgbcLight(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return ambient_environment_.GetRgbcLight(parameter_value_type);
}

float PhysicalModel::getParameterWristTilt(ParameterValueType parameter_value_type) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    return inertial_model_.GetWristTilt(parameter_value_type);
}

template <class T, class GETTER>
T PhysicalModel::getSensorValue(const AndroidSensor sensor, const T* override_member_pointer,
                                const GETTER& physical_getter, size_t* measurement_id) const {
    const auto sensor_index = static_cast<size_t>(sensor);

    const std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (use_override_[static_cast<size_t>(sensor)]) {
        *measurement_id = measurement_id_[sensor_index];
        return *override_member_pointer;
    }
    if (is_physical_state_changing_) {
        measurement_id_[sensor_index]++;
    }
    *measurement_id = measurement_id_[sensor_index];
    return physical_getter();
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
#define GOLDFISH_SENSOR_DEF(x, y, z, v, w)                                                        \
    v PhysicalModel::GET_FUNCTION_NAME(z)(size_t* measurement_id) const {                         \
        return getSensorValue<v>(                                                                 \
                SENSOR_NAME(x), &OVERRIDE_NAME(z), [this]() { return this->PHYSICAL_NAME(z)(); }, \
                measurement_id);                                                                  \
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
    return glm::conjugate(inertial_model_.GetRotation()) *
           (inertial_model_.GetAcceleration() - ambient_environment_.GetGravity());
}

vec3 PhysicalModel::getPhysicalAccelerometerUncalibrated() const {
    // Same values for the calibrated and uncalibrated accelerometer
    // Bias will be added to the values on the guest side.
    return getPhysicalAccelerometer();
}

vec3 PhysicalModel::getPhysicalGyroscope() const {
    return glm::conjugate(inertial_model_.GetRotation()) * inertial_model_.GetRotationalVelocity();
}

vec3 PhysicalModel::getPhysicalMagnetometer() const {
    return glm::conjugate(inertial_model_.GetRotation()) * ambient_environment_.GetMagneticField();
}

/* (x, y, z) == (azimuth, pitch, roll) */
vec3 PhysicalModel::getPhysicalOrientation() const {
    return ToEulerAnglesXyz(inertial_model_.GetRotation());
}

float PhysicalModel::getPhysicalTemperature() const {
    return ambient_environment_.GetTemperature();
}

float PhysicalModel::getPhysicalProximity() const {
    return ambient_environment_.GetProximity();
}

float PhysicalModel::getPhysicalLight() const {
    return ambient_environment_.GetLight();
}

float PhysicalModel::getPhysicalPressure() const {
    return ambient_environment_.GetPressure();
}

float PhysicalModel::getPhysicalHumidity() const {
    return ambient_environment_.GetHumidity();
}

vec3 PhysicalModel::getPhysicalMagnetometerUncalibrated() const {
    return glm::conjugate(inertial_model_.GetRotation()) * ambient_environment_.GetMagneticField();
}

vec3 PhysicalModel::getPhysicalGyroscopeUncalibrated() const {
    return glm::conjugate(inertial_model_.GetRotation()) * inertial_model_.GetRotationalVelocity();
}

vec4 PhysicalModel::getPhysicalRgbcLight() const {
    return ambient_environment_.GetRgbcLight();
}

void PhysicalModel::GetTransform(float* out_translation_x, float* out_translation_y,
                                 float* out_translation_z, float* out_rotation_x,
                                 float* out_rotation_y, float* out_rotation_z,
                                 int64_t* out_timestamp) const {
    const std::lock_guard<std::recursive_mutex> lock(mutex_);

    const vec3 position = getParameterPosition(ParameterValueType::kCurrent);
    *out_translation_x = position.x;
    *out_translation_y = position.y;
    *out_translation_z = position.z;
    const vec3 rotation = getParameterRotation(ParameterValueType::kCurrent);
    *out_rotation_x = rotation.x;
    *out_rotation_y = rotation.y;
    *out_rotation_z = rotation.z;
    *out_timestamp = model_time_ns_;
}

Rotation PhysicalModel::GetDeviceRotation() const {
    using physics::SkinRotation;

    size_t measurement_id;
    const vec3 device_accelerometer = getAccelerometer(&measurement_id);
    const glm::vec3 normalized_accelerometer = glm::normalize(device_accelerometer);

    static const std::array<std::pair<glm::vec3, SkinRotation>, 4> kDirections{
        std::make_pair(glm::vec3(0.0F, 1.0F, 0.0F), SkinRotation::kPortrait),
        std::make_pair(glm::vec3(1.0F, 0.0F, 0.0F), SkinRotation::kLandscape),
        std::make_pair(glm::vec3(0.0F, -1.0F, 0.0F), SkinRotation::kReversePortrait),
        std::make_pair(glm::vec3(-1.0F, 0.0F, 0.0F), SkinRotation::kReverseLandscape)};

    auto coarse_orientation = SkinRotation::kPortrait;
    for (const auto& v : kDirections) {
        if (fabs(glm::dot(normalized_accelerometer, v.first) - 1.F) < 0.1F) {
            coarse_orientation = v.second;
            break;
        }
    }

    return {
        .rotation = coarse_orientation,
        .x_axis = device_accelerometer.x,
        .y_axis = device_accelerometer.y,
        .z_axis = device_accelerometer.z,
    };
}

float PhysicalModel::getPhysicalHingeAngle0() const {
    return foldable_model_.GetHingeAngle(0);
}

float PhysicalModel::getPhysicalHingeAngle1() const {
    return foldable_model_.GetHingeAngle(1);
}

float PhysicalModel::getPhysicalHingeAngle2() const {
    return foldable_model_.GetHingeAngle(2);
}

float PhysicalModel::getPhysicalHeartRate() const {
    return body_model_.GetHeartRate();
}

float PhysicalModel::getPhysicalWristTilt() const {
    return inertial_model_.GetWristTilt();
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

void PhysicalModel::PhysicalStateChanging() {
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        // Note: We only call onPhysicalStateChanging if this is a transition
        // from stable to changing (i.e. don't call if we get to
        // physicalStateChanging calls in a row without a
        // physicalStateStabilized call in between).
        if (!is_physical_state_changing_) {
            is_physical_state_changing_ = true;
        }
    }

    const PhysicalModelChangeEvent event{
        .type = PhysicalModelChangeEvent::Type::kPhysicalStateChanging,
        .model = this,
    };
    FireEvent(event);
}

void PhysicalModel::PhysicalStateStabilized() {
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        assert(is_physical_state_changing_);

        // Increment all of the measurement ids because the physical state has
        // stabilized.
        for (size_t& i : measurement_id_) {
            i++;
        }
        is_physical_state_changing_ = false;
    }

    const PhysicalModelChangeEvent event{
        .type = PhysicalModelChangeEvent::Type::kPhysicalStateStabilized,
        .model = this,
    };
    FireEvent(event);
}

void PhysicalModel::TargetStateChanged() {
    {
        const std::lock_guard<std::recursive_mutex> lock(mutex_);
        // When target state changes we reset all sensor overrides.
        for (bool& i : use_override_) {
            i = false;
        }
    }

    const PhysicalModelChangeEvent event{
        .type = PhysicalModelChangeEvent::Type::kTargetStateChanged,
        .model = this,
    };
    FireEvent(event);
}

}  // namespace goldfish::sensors
