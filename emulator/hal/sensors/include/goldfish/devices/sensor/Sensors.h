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

#include <math.h>

#include "goldfish/devices/sensor/AndroidSensor.h"
#include "goldfish/devices/sensor/PhysicalParameter.h"

namespace goldfish::devices::sensor {

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

}  // namespace goldfish::devices::sensor
