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

#include <gtest/gtest.h>

#include <vector>

namespace goldfish::display {

TEST(ImagingTest, AbgrToRgbLe) {
    // 2 pixels:
    // Pixel 0: R=0x11, G=0x22, B=0x33, A=0xFF
    // Pixel 1: R=0xAA, G=0xBB, B=0xCC, A=0x00
    // In little-endian uint32_t ABGR:
    // Pixel 0: 0xFF332211
    // Pixel 1: 0x00CCBBAA
    std::vector<uint32_t> src = {0xFF332211, 0x00CCBBAA};
    std::vector<uint8_t> dst(6, 0);

    Imaging::AbgrToRgbLe(src.data(), dst.data(), 2);

    // Expected output: 11 22 33 AA BB CC
    EXPECT_EQ(dst[0], 0x11);
    EXPECT_EQ(dst[1], 0x22);
    EXPECT_EQ(dst[2], 0x33);
    EXPECT_EQ(dst[3], 0xAA);
    EXPECT_EQ(dst[4], 0xBB);
    EXPECT_EQ(dst[5], 0xCC);
}

}  // namespace goldfish::display
