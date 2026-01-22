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

#include "goldfish/physics/skin_rotation.h"

namespace goldfish::physics {

struct Rotation {
    SkinRotation rotation;

    float x_axis;  ///< The x-axis acceleration value (in m/s^2).
    float y_axis;  ///< The y-axis acceleration value (in m/s^2).
    float z_axis;  ///< The z-axis acceleration value (in m/s^2).
};

}  // namespace goldfish::physics
