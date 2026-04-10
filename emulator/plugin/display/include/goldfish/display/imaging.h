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

namespace goldfish::display {

/**
 * @brief Imaging utilities for display.
 */
class Imaging {
  public:
    /**
     * @brief Rapidly converts an ABGR (little-endian) buffer to RGB (little-endian).
     *
     * This method skips the alpha channel and assumes both source and destination
     * buffers are in little-endian format.
     *
     * @param src Pointer to the source ABGR32 buffer.
     * @param dst Pointer to the destination RGB24 buffer.
     * @param num_pixels Number of pixels to convert.
     */
    static void AbgrToRgbLe(const uint32_t* src, uint8_t* dst, int num_pixels);
};

}  // namespace goldfish::display
