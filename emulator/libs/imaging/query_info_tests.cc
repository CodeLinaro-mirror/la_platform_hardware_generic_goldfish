/* Copyright 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include "goldfish/imaging/query_info.h"

using namespace goldfish::imaging;

TEST(GetStride, format_width) {
    EXPECT_EQ(GetStride(ImageFormat::kRgba8888, 7), 28);
    EXPECT_EQ(GetStride(ImageFormat::kYuV4203P, 7), 0);
    EXPECT_EQ(GetStride(ImageFormat::kYuV420NV12, 7), 0);
    EXPECT_EQ(GetStride(ImageFormat::kNone, 7), 0);
}

TEST(GetStride, image) {
    EXPECT_EQ(GetStride(ImageRef(ImageFormat::kRgba8888, {7, 7}, nullptr, 0)), 28);
    EXPECT_EQ(GetStride(ImageRef(ImageFormat::kYuV4203P, {7, 7}, nullptr, 0)), 0);
    EXPECT_EQ(GetStride(ImageRef(ImageFormat::kYuV420NV12, {7, 7}, nullptr, 0)), 0);
    EXPECT_EQ(GetStride(ImageRef(ImageFormat::kNone, {7, 7}, nullptr, 0)), 0);
}

TEST(GetDataSize, incorrect_format) {
    EXPECT_EQ(GetDataSize(static_cast<ImageFormat>(1377354), 42, 97), 0);
}

TEST(GetDataSize, empty_format) {
    EXPECT_EQ(GetDataSize(ImageFormat::kNone, 42, 97), 0);
}

TEST(GetDataSize, rgbx) {
    EXPECT_EQ(GetDataSize(ImageFormat::kRgba8888, 42, 97), 42 * 97 * 4);
}

TEST(GetDataSize, yuv) {
    EXPECT_EQ(GetDataSize(ImageFormat::kYuV4203P, 300, 200), 300 * 200 * 3 / 2);
    EXPECT_EQ(GetDataSize(ImageFormat::kYuV420NV12, 300, 200), 300 * 200 * 3 / 2);
}
