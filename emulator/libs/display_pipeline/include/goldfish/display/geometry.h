// Copyright 2025 The Android Open Source Project
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

#include <cstdint>

#include "goldfish/physics/skin_rotation.h"

namespace goldfish::display {

/**
 * Width and height of a display surface, crop region, or output image in pixels.
 */
struct Dimensions {
    uint32_t width = 0;
    uint32_t height = 0;

    friend bool operator==(const Dimensions&, const Dimensions&) = default;
};

/**
 * Counter-clockwise rotation angle applied when reading a frame.
 *
 * Restricting rotations to orthogonal 90-degree multiples ensures that
 * coordinate transformations (swapping width/height and mirroring axes) are
 * exact and never introduce resampling artifacts.
 */
enum class ImageRotation : std::uint16_t {
    kRotation0 = 0,
    kRotation90 = 90,
    kRotation180 = 180,
    kRotation270 = 270,
};

/**
 * Converts a physical device posture rotation (`SkinRotation`) into the
 * corresponding counter-clockwise `ImageRotation` needed to orient the
 * display's native framebuffer for the viewer.
 *
 * @param r The physical device rotation reported by the posture/physics model.
 * @return The counter-clockwise image rotation angle.
 */
constexpr ImageRotation ToImageRotation(physics::SkinRotation r) {
    switch (r) {
    case physics::SkinRotation::kPortrait:
        return ImageRotation::kRotation0;
    case physics::SkinRotation::kLandscape:
        return ImageRotation::kRotation90;
    case physics::SkinRotation::kReversePortrait:
        return ImageRotation::kRotation180;
    case physics::SkinRotation::kReverseLandscape:
        return ImageRotation::kRotation270;
    }
    return ImageRotation::kRotation0;
}

}  // namespace goldfish::display
