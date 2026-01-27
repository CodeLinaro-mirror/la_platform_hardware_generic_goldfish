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

Orientation IDisplay::getOrientation(int w, int h) {
    if (w > h) return Orientation::kLandscape;
    if (h > w) return Orientation::kPortrait;
    return Orientation::kSquare;
}

IDisplay::LogicalFit IDisplay::calculateLogicalFit(int desiredWidth, int desiredHeight) const {
    Dimensions dims = GetDimensions();
    if (dims.width <= 0 || dims.height <= 0 || desiredWidth <= 0 || desiredHeight <= 0) {
        return {0, 0, false};
    }

    // We determine the source orientation based on the requested box.
    // If the box is rectangular (not square) and its orientation differs
    // from the physical display, we swap the source dimensions to calculate
    // the logical fit. Square sources or square boxes do not trigger a swap.
    Orientation boxOri = getOrientation(desiredWidth, desiredHeight);
    Orientation sourceOri = getOrientation(dims.width, dims.height);

    bool swapped = (boxOri == Orientation::kLandscape && sourceOri == Orientation::kPortrait) ||
                   (boxOri == Orientation::kPortrait && sourceOri == Orientation::kLandscape);

    int64_t sWidth = swapped ? dims.height : dims.width;
    int64_t sHeight = swapped ? dims.width : dims.height;

    // Note that we will never scale above logical display device width and height.
    desiredWidth = std::min<int64_t>(desiredWidth, sWidth);
    desiredHeight = std::min<int64_t>(desiredHeight, sHeight);

    if (static_cast<int64_t>(desiredWidth) * sHeight <
        static_cast<int64_t>(desiredHeight) * sWidth) {
        // Width is the limiting factor.
        int newHeight = static_cast<int>((sHeight * desiredWidth) / sWidth);
        return {desiredWidth, newHeight, swapped};
    } else {
        // Height is the limiting factor.
        int newWidth = static_cast<int>((sWidth * desiredHeight) / sHeight);
        return {newWidth, desiredHeight, swapped};
    }
}

std::pair<int, int> IDisplay::resizeKeepAspectRatio(int desiredWidth, int desiredHeight) {
    auto fit = calculateLogicalFit(desiredWidth, desiredHeight);
    return {fit.width, fit.height};
}

std::string IDisplay::string() const {
    Dimensions dims = GetDimensions();
    return absl::StrFormat("Display: %d (%dx%d), seq: %u", mDisplayId, dims.width, dims.height,
                           seq().sequenceNumber);
}


}  // namespace goldfish::display
