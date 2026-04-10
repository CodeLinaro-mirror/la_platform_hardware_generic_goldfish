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

#include "goldfish/display/imaging.h"

namespace goldfish::display {

void Imaging::AbgrToRgbLe(const uint32_t* src, uint8_t* dst, int num_pixels) {
    const auto* byte_src = reinterpret_cast<const uint8_t*>(src);

    for (int i = 0; i < num_pixels; i++) {
        // Source index: jumps 4 bytes at a time (skip Alpha)
        // Dest index: jumps 3 bytes at a time
        dst[(i * 3) + 0] = byte_src[(i * 4) + 0];  // R
        dst[(i * 3) + 1] = byte_src[(i * 4) + 1];  // G
        dst[(i * 3) + 2] = byte_src[(i * 4) + 2];  // B
        // Skip byte_src[i*4 + 3] (Alpha)
    }
}

}  // namespace goldfish::display
