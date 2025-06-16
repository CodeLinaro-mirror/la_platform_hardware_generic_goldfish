// Copyright 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/* struct for use in sensor values */
struct vec3 {
    float x;
    float y;
    float z;
    bool operator==(const vec3& rhs) const {
        const float kEpsilon = 0.00001f;

        const double diffX = fabs(x - rhs.x);
        const double diffY = fabs(y - rhs.y);
        const double diffZ = fabs(z - rhs.z);
        return (diffX < kEpsilon && diffY < kEpsilon && diffZ < kEpsilon);
    }

    bool operator!=(const vec3& rhs) const { return !(*this == rhs); }
};

/* struct for use in sensor values */
struct vec4 {
    float x;
    float y;
    float z;
    float w;

    bool operator==(const vec4& rhs) const {
        const float kEpsilon = 0.00001f;

        const double diffX = fabs(x - rhs.x);
        const double diffY = fabs(y - rhs.y);
        const double diffZ = fabs(z - rhs.z);
        const double diffW = fabs(w - rhs.w);
        return (diffX < kEpsilon && diffY < kEpsilon && diffZ < kEpsilon && diffW < kEpsilon);
    }

    bool operator!=(const vec4& rhs) const { return !(*this == rhs); }
};

/* NOTE: this list must be the same that the one defined in
 *       the sensors_qemu.c source of the libsensors.goldfish.so
 *       library.
 *
 *       DO NOT CHANGE THE ORDER IN THIS LIST, UNLESS YOU INTEND
 *       TO BREAK SNAPSHOTS!
 */
#define SENSORS_LIST                                                                              \
    SENSOR_(ACCELERATION, "acceleration", Accelerometer, vec3, "acceleration:%g:%g:%g")           \
    SENSOR_(GYROSCOPE, "gyroscope", Gyroscope, vec3, "gyroscope:%g:%g:%g")                        \
    SENSOR_(MAGNETIC_FIELD, "magnetic-field", Magnetometer, vec3, "magnetic:%g:%g:%g")            \
    SENSOR_(ORIENTATION, "orientation", Orientation, vec3, "orientation:%g:%g:%g")                \
    SENSOR_(TEMPERATURE, "temperature", Temperature, float, "temperature:%g")                     \
    SENSOR_(PROXIMITY, "proximity", Proximity, float, "proximity:%g")                             \
    SENSOR_(LIGHT, "light", Light, float, "light:%g")                                             \
    SENSOR_(PRESSURE, "pressure", Pressure, float, "pressure:%g")                                 \
    SENSOR_(HUMIDITY, "humidity", Humidity, float, "humidity:%g")                                 \
    SENSOR_(MAGNETIC_FIELD_UNCALIBRATED, "magnetic-field-uncalibrated", MagnetometerUncalibrated, \
            vec3, "magnetic-uncalibrated:%g:%g:%g")                                               \
    SENSOR_(GYROSCOPE_UNCALIBRATED, "gyroscope-uncalibrated", GyroscopeUncalibrated, vec3,        \
            "gyroscope-uncalibrated:%g:%g:%g")                                                    \
    SENSOR_(HINGE_ANGLE0, "hinge-angle0", HingeAngle0, float, "hinge-angle0:%g")                  \
    SENSOR_(HINGE_ANGLE1, "hinge-angle1", HingeAngle1, float, "hinge-angle1:%g")                  \
    SENSOR_(HINGE_ANGLE2, "hinge-angle2", HingeAngle2, float, "hinge-angle2:%g")                  \
    SENSOR_(HEART_RATE, "heart-rate", HeartRate, float, "heart-rate:%g")                          \
    SENSOR_(RGBC_LIGHT, "rgbc-light", RgbcLight, vec4, "rgbc-light:%g:%g:%g:%g")                  \
    SENSOR_(WRIST_TILT, "wrist-tilt", WristTilt, float, "wrist-tilt:%g")                          \
    SENSOR_(ACCELERATION_UNCALIBRATED, "acceleration-uncalibrated", AccelerometerUncalibrated,    \
            vec3, "acceleration-uncalibrated:%g:%g:%g")

enum class AndroidSensor {
#define SENSOR_(x, y, z, v, w) x,
    SENSORS_LIST
#undef SENSOR_
            MAX_SENSORS /* do not remove */
};

/*
 * Note: DO NOT CHANGE THE ORDER IN THIS LIST, UNLESS YOU INTEND
 *       TO BREAK SNAPSHOTS!
 */
#define PHYSICAL_PARAMETERS_LIST                                                                   \
    PHYSICAL_PARAMETER_(POSITION, "position", Position, vec3)                                      \
    PHYSICAL_PARAMETER_(ROTATION, "rotation", Rotation, vec3)                                      \
    PHYSICAL_PARAMETER_(MAGNETIC_FIELD, "magnetic-field", MagneticField, vec3)                     \
    PHYSICAL_PARAMETER_(TEMPERATURE, "temperature", Temperature, float)                            \
    PHYSICAL_PARAMETER_(PROXIMITY, "proximity", Proximity, float)                                  \
    PHYSICAL_PARAMETER_(LIGHT, "light", Light, float)                                              \
    PHYSICAL_PARAMETER_(PRESSURE, "pressure", Pressure, float)                                     \
    PHYSICAL_PARAMETER_(HUMIDITY, "humidity", Humidity, float)                                     \
    PHYSICAL_PARAMETER_(VELOCITY, "velocity", Velocity, vec3)                                      \
    PHYSICAL_PARAMETER_(AMBIENT_MOTION, "ambientMotion", AmbientMotion, float)                     \
    PHYSICAL_PARAMETER_(HINGE_ANGLE0, "hinge-angle0", HingeAngle0, float)                          \
    PHYSICAL_PARAMETER_(HINGE_ANGLE1, "hinge-angle1", HingeAngle1, float)                          \
    PHYSICAL_PARAMETER_(HINGE_ANGLE2, "hinge-angle2", HingeAngle2, float)                          \
    PHYSICAL_PARAMETER_(ROLLABLE0, "rollable0", Rollable0, float)                                  \
    PHYSICAL_PARAMETER_(ROLLABLE1, "rollable1", Rollable1, float)                                  \
    PHYSICAL_PARAMETER_(ROLLABLE2, "rollable2", Rollable2, float)                                  \
    PHYSICAL_PARAMETER_(POSTURE, "posture", Posture, float)                                        \
    PHYSICAL_PARAMETER_(HEART_RATE, "heart-rate", HeartRate, float)                                \
    PHYSICAL_PARAMETER_(RGBC_LIGHT, "rgbc-light", RgbcLight, vec4)                                 \
    PHYSICAL_PARAMETER_(WRIST_TILT, "wrist-tilt", WristTilt, float)                                \
    PHYSICAL_PARAMETER_(ROTATION_UNCALIBRATED, "rotation-uncalibrated", AccelerometerUncalibrated, \
                        vec3)

enum class PhysicalParameter {
#define PHYSICAL_PARAMETER_(x, y, z, w) x,
    PHYSICAL_PARAMETERS_LIST
#undef PHYSICAL_PARAMETER_
            MAX_PHYSICAL_PARAMETERS
};
