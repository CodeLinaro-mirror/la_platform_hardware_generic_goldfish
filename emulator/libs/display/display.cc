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

namespace goldfish::display {

std::pair<int, int> IDisplay::resizeKeepAspectRatio(int desiredWidth, int desiredHeight) {
    if (mWidth <= 0 || mHeight <= 0) {
        return {0, 0};
    }

    // Use 64-bit integers for the cross-multiplication to prevent overflow.
    int64_t h64 = mHeight;
    int64_t w64 = mWidth;

    // Note that we will never scale above display device width and height.
    desiredWidth = std::min<int64_t>(desiredWidth, w64);
    desiredHeight = std::min<int64_t>(desiredHeight, h64);

    if (static_cast<int64_t>(desiredWidth) * h64 < static_cast<int64_t>(desiredHeight) * w64) {
        // Width is the limiting factor, so we scale to the desired width.
        int newHeight = static_cast<int>((h64 * desiredWidth) / w64);
        return {desiredWidth, newHeight};
    } else {
        // Height is the limiting factor, so we scale to the desired height.
        int newWidth = static_cast<int>((w64 * desiredHeight) / h64);
        return {newWidth, desiredHeight};
    }
}

}  // namespace goldfish::display
