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

#include "goldfish/imaging/queryInfo.h"

using namespace goldfish::imaging;

TEST(getStride, format_width) {
    EXPECT_EQ(getStride(ImageFormat::RGBA_8888, 7), 28);
    EXPECT_EQ(getStride(ImageFormat::YUV420_3P, 7), 0);
    EXPECT_EQ(getStride(ImageFormat::YUV420_NV12, 7), 0);
    EXPECT_EQ(getStride(ImageFormat::NONE, 7), 0);
}

TEST(getStride, image) {
    EXPECT_EQ(getStride(ImageRef(ImageFormat::RGBA_8888, {7, 7}, nullptr, 0)), 28);
    EXPECT_EQ(getStride(ImageRef(ImageFormat::YUV420_3P, {7, 7}, nullptr, 0)), 0);
    EXPECT_EQ(getStride(ImageRef(ImageFormat::YUV420_NV12, {7, 7}, nullptr, 0)), 0);
    EXPECT_EQ(getStride(ImageRef(ImageFormat::NONE, {7, 7}, nullptr, 0)), 0);
}

TEST(getDataSize, incorrect_format) {
    EXPECT_EQ(getDataSize(static_cast<ImageFormat>(1377354), 42, 97), 0);
}

TEST(getDataSize, empty_format) {
    EXPECT_EQ(getDataSize(ImageFormat::NONE, 42, 97), 0);
}

TEST(getDataSize, rgbx) {
    EXPECT_EQ(getDataSize(ImageFormat::RGBA_8888, 42, 97), 42 * 97 * 4);
}

TEST(getDataSize, yuv) {
    EXPECT_EQ(getDataSize(ImageFormat::YUV420_3P, 300, 200), 300 * 200 * 3 / 2);
    EXPECT_EQ(getDataSize(ImageFormat::YUV420_NV12, 300, 200), 300 * 200 * 3 / 2);
}
