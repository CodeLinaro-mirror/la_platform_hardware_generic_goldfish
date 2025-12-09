/*
 * Copyright (C) 2025 The Android Open Source Project
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

/*
 * Note: DO NOT CHANGE THE ORDER IN THIS LIST, UNLESS YOU INTEND
 *       TO BREAK SNAPSHOTS!
 */
#define GOLDFISH_PHYSICAL_PARAMETERS_LIST                                                  \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(POSITION, "position", Position, vec3)                  \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(ROTATION, "rotation", Rotation, vec3)                  \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(MAGNETIC_FIELD, "magnetic-field", MagneticField, vec3) \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(TEMPERATURE, "temperature", Temperature, float)        \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(PROXIMITY, "proximity", Proximity, float)              \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(LIGHT, "light", Light, float)                          \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(PRESSURE, "pressure", Pressure, float)                 \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(HUMIDITY, "humidity", Humidity, float)                 \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(VELOCITY, "velocity", Velocity, vec3)                  \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(AMBIENT_MOTION, "ambientMotion", AmbientMotion, float) \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(HINGE_ANGLE0, "hinge-angle0", HingeAngle0, float)      \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(HINGE_ANGLE1, "hinge-angle1", HingeAngle1, float)      \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(HINGE_ANGLE2, "hinge-angle2", HingeAngle2, float)      \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(ROLLABLE0, "rollable0", Rollable0, float)              \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(ROLLABLE1, "rollable1", Rollable1, float)              \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(ROLLABLE2, "rollable2", Rollable2, float)              \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(POSTURE, "posture", Posture, float)                    \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(HEART_RATE, "heart-rate", HeartRate, float)            \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(RGBC_LIGHT, "rgbc-light", RgbcLight, vec4)             \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(WRIST_TILT, "wrist-tilt", WristTilt, float)            \
    GOLDFISH_PHYSICAL_PARAMETER_DEF(ROTATION_UNCALIBRATED, "rotation-uncalibrated",        \
                                    AccelerometerUncalibrated, vec3)
