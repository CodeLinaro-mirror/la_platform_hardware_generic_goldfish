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

#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstdint>

#include "goldfish/physics/physics.h"

namespace goldfish::physics {

enum class AmbientState : std::uint8_t {
    kChanging = 0,
    kStable = 1,
};

/*
 * Implements a model of an ambient environment, within which sensor
 * values could be calculated related to the ambient state (e.g. the
 * ambient gravity vector, magnetic vector, etc...).
 *
 * The ambient environment should be created, and its attribute targets
 * should be set as desired.  When sensor values are to be calculated
 * and require an ambient attribute as input, that attribute should be
 * polled from the ambient environment to get the most recent state.
 */
class AmbientEnvironment {
  public:
    AmbientEnvironment() = default;

    /*
     * Sets the strength of the ambient magnetic field.
     */
    void SetMagneticField(float north, float east, float vertical, PhysicalInterpolation mode);

    /*
     * Sets the ambient gravity vector.
     */
    void SetGravity(glm::vec3 gravity, PhysicalInterpolation mode);

    /*
     * Sets the ambient temperature.
     */
    void SetTemperature(float celsius, PhysicalInterpolation mode);

    /*
     * Sets the target proximity value.
     */
    void SetProximity(float centimeters, PhysicalInterpolation mode);

    /*
     * Sets the target ambient light value.
     */
    void SetLight(float lux, PhysicalInterpolation mode);

    /*
     * Sets the target barometric pressure value.
     */
    void SetPressure(float pascal, PhysicalInterpolation mode);

    /*
     * Sets the target humidity value.
     */
    void SetHumidity(float percent, PhysicalInterpolation mode);

    /*
     * Sets the target ambient light value.
     */
    void SetRgbcLight(glm::vec4 light, PhysicalInterpolation mode);

    /*
     * Gets current simulated state of the ambient environment.
     */
    glm::vec3 GetMagneticField(
            ParameterValueType parameter_value_type = ParameterValueType::kCurrent) const;
    glm::vec3 GetGravity(
            ParameterValueType parameter_value_type = ParameterValueType::kCurrent) const;
    float GetTemperature(
            ParameterValueType parameter_value_type = ParameterValueType::kCurrent) const;
    float GetProximity(
            ParameterValueType parameter_value_type = ParameterValueType::kCurrent) const;
    float GetLight(ParameterValueType parameter_value_type = ParameterValueType::kCurrent) const;
    float GetPressure(ParameterValueType parameter_value_type = ParameterValueType::kCurrent) const;
    float GetHumidity(ParameterValueType parameter_value_type = ParameterValueType::kCurrent) const;
    glm::vec4 GetRgbcLight(
            ParameterValueType parameter_value_type = ParameterValueType::kCurrent) const;

  private:
    static constexpr glm::vec3 kDefaultMagneticField = glm::vec3(0.0F, 5.9F, -48.4F);
    static constexpr glm::vec3 kDefaultGravity = glm::vec3(0.F, -9.81F, 0.F);

    /* celsius */
    static constexpr float kDefaultTemperature = 0.F;
    /* cm */
    static constexpr float kDefaultProximity = 1.F;
    /* lux */
    static constexpr float kDefaultLight = 0.F;
    /* hPa */
    static constexpr float kDefaultPressure = 0.F;
    /* percent */
    static constexpr float kDefaultHumidity = 0.F;
    /* raw RGBC value */
    static constexpr glm::vec4 kDefaultRgbcLight = glm::vec4(glm::vec3(0, 0, 0), 0);

    glm::vec3 magnetic_field_ = kDefaultMagneticField;
    glm::vec3 gravity_ = kDefaultGravity;
    float temperature_ = kDefaultTemperature;
    float proximity_ = kDefaultProximity;
    float light_ = kDefaultLight;
    float pressure_ = kDefaultPressure;
    float humidity_ = kDefaultHumidity;
    glm::vec4 rgbc_light_ = kDefaultRgbcLight;
};

}  // namespace goldfish::physics
