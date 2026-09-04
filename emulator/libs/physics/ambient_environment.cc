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

#include "goldfish/archive/glm.h"

namespace goldfish::physics {

void AmbientEnvironment::SetMagneticField(float north, float east, float vertical,
                                          PhysicalInterpolation /*mode*/) {
    magnetic_field_ = glm::vec3(north, east, vertical);
}

void AmbientEnvironment::SetGravity(glm::vec3 gravity, PhysicalInterpolation /*mode*/) {
    gravity_ = gravity;
}

void AmbientEnvironment::SetTemperature(float celsius, PhysicalInterpolation /*mode*/) {
    temperature_ = celsius;
}

void AmbientEnvironment::SetProximity(float centimeters, PhysicalInterpolation /*mode*/) {
    proximity_ = centimeters;
}

void AmbientEnvironment::SetLight(float lux, PhysicalInterpolation /*mode*/) {
    light_ = lux;
}

void AmbientEnvironment::SetPressure(float pascal, PhysicalInterpolation /*mode*/) {
    pressure_ = pascal;
}

void AmbientEnvironment::SetHumidity(float percent, PhysicalInterpolation /*mode*/) {
    humidity_ = percent;
}

void AmbientEnvironment::SetRgbcLight(glm::vec4 light, PhysicalInterpolation /*mode*/) {
    rgbc_light_ = light;
}

glm::vec3 AmbientEnvironment::GetMagneticField(ParameterValueType value_type) const {
    return value_type == ParameterValueType::kDefault ? kDefaultMagneticField : magnetic_field_;
}

glm::vec3 AmbientEnvironment::GetGravity(ParameterValueType value_type) const {
    return value_type == ParameterValueType::kDefault ? kDefaultGravity : gravity_;
}

float AmbientEnvironment::GetTemperature(ParameterValueType value_type) const {
    return value_type == ParameterValueType::kDefault ? kDefaultTemperature : temperature_;
}

float AmbientEnvironment::GetProximity(ParameterValueType value_type) const {
    return value_type == ParameterValueType::kDefault ? kDefaultProximity : proximity_;
}

float AmbientEnvironment::GetLight(ParameterValueType value_type) const {
    return value_type == ParameterValueType::kDefault ? kDefaultLight : light_;
}

float AmbientEnvironment::GetPressure(ParameterValueType value_type) const {
    return value_type == ParameterValueType::kDefault ? kDefaultPressure : pressure_;
}

float AmbientEnvironment::GetHumidity(ParameterValueType value_type) const {
    return value_type == ParameterValueType::kDefault ? kDefaultHumidity : humidity_;
}

glm::vec4 AmbientEnvironment::GetRgbcLight(ParameterValueType value_type) const {
    return value_type == ParameterValueType::kDefault ? kDefaultRgbcLight : rgbc_light_;
}

archive::IWriter& operator<<(archive::IWriter& w, const AmbientEnvironment& ae) {
    return w << ae.magnetic_field_ << ae.gravity_ << ae.temperature_ << ae.proximity_ << ae.light_
             << ae.pressure_ << ae.humidity_ << ae.rgbc_light_;
}

absl::Status ReadValue(archive::IReader& r, AmbientEnvironment& ae) {
    return ReadValue(r, ae.magnetic_field_, ae.gravity_, ae.temperature_, ae.proximity_, ae.light_,
                     ae.pressure_, ae.humidity_, ae.rgbc_light_);
}

}  // namespace goldfish::physics
