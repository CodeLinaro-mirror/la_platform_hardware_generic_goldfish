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

namespace goldfish {
namespace physics {

enum class SkinRotation {
    PORTRAIT = 0,           // Portrait orientation (0 degrees).
    LANDSCAPE = 1,          // Landscape orientation (90 degrees clockwise).
    REVERSE_PORTRAIT = 2,   // Reverse portrait orientation (180 degrees or -180 degrees).
    REVERSE_LANDSCAPE = 3,  // Reverse landscape orientation (270 degrees clockwise or -90 degrees).
};

}  // namespace physics
}  // namespace goldfish
