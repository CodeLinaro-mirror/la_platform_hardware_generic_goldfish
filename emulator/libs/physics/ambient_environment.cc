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

#include "goldfish/physics/ambient_environment.h"

namespace goldfish {
namespace physics {

constexpr glm::vec3 AmbientEnvironment::kDefaultMagneticField;
constexpr glm::vec3 AmbientEnvironment::kDefaultGravity;

AmbientState AmbientEnvironment::setCurrentTime(uint64_t time_ns) {
    return AmbientState::STABLE;
}

void AmbientEnvironment::setMagneticField(float north, float east, float vertical,
                                          PhysicalInterpolation mode) {
    mMagneticField = glm::vec3(north, east, vertical);
}

void AmbientEnvironment::setGravity(glm::vec3 gravity, PhysicalInterpolation mode) {
    mGravity = gravity;
}

void AmbientEnvironment::setTemperature(float celsius, PhysicalInterpolation mode) {
    mTemperature = celsius;
}

void AmbientEnvironment::setProximity(float centimeters, PhysicalInterpolation mode) {
    mProximity = centimeters;
}

void AmbientEnvironment::setLight(float lux, PhysicalInterpolation mode) {
    mLight = lux;
}

void AmbientEnvironment::setPressure(float hPa, PhysicalInterpolation mode) {
    mPressure = hPa;
}

void AmbientEnvironment::setHumidity(float percent, PhysicalInterpolation mode) {
    mHumidity = percent;
}

void AmbientEnvironment::setRgbcLight(glm::vec4 light, PhysicalInterpolation mode) {
    mRgbcLight = light;
}

glm::vec3 AmbientEnvironment::getMagneticField(ParameterValueType valueType) const {
    return valueType == ParameterValueType::DEFAULT ? kDefaultMagneticField : mMagneticField;
}

glm::vec3 AmbientEnvironment::getGravity(ParameterValueType valueType) const {
    return valueType == ParameterValueType::DEFAULT ? kDefaultGravity : mGravity;
}

float AmbientEnvironment::getTemperature(ParameterValueType valueType) const {
    return valueType == ParameterValueType::DEFAULT ? kDefaultTemperature : mTemperature;
}

float AmbientEnvironment::getProximity(ParameterValueType valueType) const {
    return valueType == ParameterValueType::DEFAULT ? kDefaultProximity : mProximity;
}

float AmbientEnvironment::getLight(ParameterValueType valueType) const {
    return valueType == ParameterValueType::DEFAULT ? kDefaultLight : mLight;
}

float AmbientEnvironment::getPressure(ParameterValueType valueType) const {
    return valueType == ParameterValueType::DEFAULT ? kDefaultPressure : mPressure;
}

float AmbientEnvironment::getHumidity(ParameterValueType valueType) const {
    return valueType == ParameterValueType::DEFAULT ? kDefaultHumidity : mHumidity;
}

glm::vec4 AmbientEnvironment::getRgbcLight(ParameterValueType valueType) const {
    return valueType == ParameterValueType::DEFAULT ? kDefaultRgbcLight : mRgbcLight;
}

}  // namespace physics
}  // namespace goldfish
