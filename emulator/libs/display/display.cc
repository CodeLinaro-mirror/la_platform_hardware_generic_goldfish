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

#include "goldfish/display/display.h"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"

namespace goldfish::display {

Orientation IDisplay::GetOrientation(int w, int h) {
    if (w > h) return Orientation::kLandscape;
    if (h > w) return Orientation::kPortrait;
    return Orientation::kSquare;
}

IDisplay::LogicalFit IDisplay::CalculateLogicalFit(int desired_width, int desired_height) const {
    const Dimensions dims = GetDimensions();
    if (dims.width <= 0 || dims.height <= 0 || desired_width <= 0 || desired_height <= 0) {
        return {.width = 0, .height = 0, .swapped = false};
    }

    // We determine the source orientation based on the requested box.
    // If the box is rectangular (not square) and its orientation differs
    // from the physical display, we swap the source dimensions to calculate
    // the logical fit. Square sources or square boxes do not trigger a swap.
    const Orientation box_ori = GetOrientation(desired_width, desired_height);
    const Orientation source_ori =
            GetOrientation(static_cast<int>(dims.width), static_cast<int>(dims.height));

    const bool swapped =
            (box_ori == Orientation::kLandscape && source_ori == Orientation::kPortrait) ||
            (box_ori == Orientation::kPortrait && source_ori == Orientation::kLandscape);

    const int64_t s_width = swapped ? dims.height : dims.width;
    const int64_t s_height = swapped ? dims.width : dims.height;

    // Note that we will never scale above logical display device width and height.
    desired_width = static_cast<int>(std::min<int64_t>(desired_width, s_width));
    desired_height = static_cast<int>(std::min<int64_t>(desired_height, s_height));

    if (static_cast<int64_t>(desired_width) * s_height <
        static_cast<int64_t>(desired_height) * s_width) {
        // Width is the limiting factor.
        const int new_height = static_cast<int>((s_height * desired_width) / s_width);
        return {.width = desired_width, .height = new_height, .swapped = swapped};
    }  // Height is the limiting factor.
    const int new_width = static_cast<int>((s_width * desired_height) / s_height);
    return {.width = new_width, .height = desired_height, .swapped = swapped};
}

std::pair<int, int> IDisplay::ResizeKeepAspectRatio(int desired_width, int desired_height) {
    auto fit = CalculateLogicalFit(desired_width, desired_height);
    return {fit.width, fit.height};
}

std::string IDisplay::String() const {
    const Dimensions dims = GetDimensions();
    return absl::StrFormat("Display: %d (%dx%d), seq: %u", display_id_, dims.width, dims.height,
                           Seq().sequence_number);
}

}  // namespace goldfish::display
