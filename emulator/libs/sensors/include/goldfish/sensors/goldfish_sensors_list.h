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

// NOLINTBEGIN
/* NOTE: this list must be the same that the one defined in
 *       the sensors_qemu.c source of the libsensors.goldfish.so
 *       library.
 *
 *       DO NOT CHANGE THE ORDER IN THIS LIST, UNLESS YOU INTEND
 *       TO BREAK SNAPSHOTS!
 */
#define GOLDFISH_SENSORS_LIST                                                                    \
    GOLDFISH_SENSOR_DEF(ACCELERATION, "acceleration", Accelerometer, vec3, "acceleration")       \
    GOLDFISH_SENSOR_DEF(GYROSCOPE, "gyroscope", Gyroscope, vec3, "gyroscope")                    \
    GOLDFISH_SENSOR_DEF(MAGNETIC_FIELD, "magnetic-field", Magnetometer, vec3, "magnetic")        \
    GOLDFISH_SENSOR_DEF(ORIENTATION, "orientation", Orientation, vec3, "orientation")            \
    GOLDFISH_SENSOR_DEF(TEMPERATURE, "temperature", Temperature, float, "temperature")           \
    GOLDFISH_SENSOR_DEF(PROXIMITY, "proximity", Proximity, float, "proximity")                   \
    GOLDFISH_SENSOR_DEF(LIGHT, "light", Light, float, "light")                                   \
    GOLDFISH_SENSOR_DEF(PRESSURE, "pressure", Pressure, float, "pressure")                       \
    GOLDFISH_SENSOR_DEF(HUMIDITY, "humidity", Humidity, float, "humidity")                       \
    GOLDFISH_SENSOR_DEF(MAGNETIC_FIELD_UNCALIBRATED, "magnetic-field-uncalibrated",              \
                        MagnetometerUncalibrated, vec3, "magnetic-uncalibrated")                 \
    GOLDFISH_SENSOR_DEF(GYROSCOPE_UNCALIBRATED, "gyroscope-uncalibrated", GyroscopeUncalibrated, \
                        vec3, "gyroscope-uncalibrated")                                          \
    GOLDFISH_SENSOR_DEF(HINGE_ANGLE0, "hinge-angle0", HingeAngle0, float, "hinge-angle0")        \
    GOLDFISH_SENSOR_DEF(HINGE_ANGLE1, "hinge-angle1", HingeAngle1, float, "hinge-angle1")        \
    GOLDFISH_SENSOR_DEF(HINGE_ANGLE2, "hinge-angle2", HingeAngle2, float, "hinge-angle2")        \
    GOLDFISH_SENSOR_DEF(HEART_RATE, "heart-rate", HeartRate, float, "heart-rate")                \
    GOLDFISH_SENSOR_DEF(RGBC_LIGHT, "rgbc-light", RgbcLight, vec4, "rgbc-light")                 \
    GOLDFISH_SENSOR_DEF(WRIST_TILT, "wrist-tilt", WristTilt, float, "wrist-tilt")                \
    GOLDFISH_SENSOR_DEF(ACCELERATION_UNCALIBRATED, "acceleration-uncalibrated",                  \
                        AccelerometerUncalibrated, vec3, "acceleration-uncalibrated")
// NOLINTEND