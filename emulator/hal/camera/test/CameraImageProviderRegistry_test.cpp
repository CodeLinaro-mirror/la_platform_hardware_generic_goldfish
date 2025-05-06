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

#include "android/camera/CameraImageProviderRegistry.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace goldfish::devices::camera;

namespace {
const CameraImageProviderInfoVtbl kCameraImageProviderInfoVtbl = {};

struct CameraImageProviderRegistryTest : public ::testing::Test {
    void SetUp() override {
        static const CameraImageProviderRect sizes0[] = {
            {
                .width = 11,
                .height = 22,
            },
            {
                .width = 33,
                .height = 44,
            },
        };

        CameraImageProviderInfo info0 = {
            .vtbl = &kCameraImageProviderInfoVtbl,
            .supportedFrameSizes = sizes0,
            .createArg = nullptr,
            .supportedFrameSizesNum = 2,
            .isBackFacing = 1,
            .needFreeSupportedSizes = 0,
        };
        mRegistry.add(info0);

        static const CameraImageProviderRect sizes1[] = {
            {
                .width = 55,
                .height = 66,
            },
        };

        CameraImageProviderInfo info1 = {
            .vtbl = &kCameraImageProviderInfoVtbl,
            .supportedFrameSizes = sizes1,
            .createArg = nullptr,
            .supportedFrameSizesNum = 1,
            .isBackFacing = 0,
            .needFreeSupportedSizes = 0,
        };

        mRegistry.add(info1);
    }

    CameraImageProviderRegistry mRegistry;
};
}  // namespace

TEST_F(CameraImageProviderRegistryTest, enumerate) {
    const auto& infos = mRegistry.enumerate();

    EXPECT_EQ(infos.size(), 2);
    EXPECT_EQ(infos[0].getInfo().supportedFrameSizesNum, 2);
    EXPECT_EQ(infos[1].getInfo().supportedFrameSizesNum, 1);
}

TEST_F(CameraImageProviderRegistryTest, clear) {
    EXPECT_EQ(mRegistry.enumerate().size(), 2);
    mRegistry.clear();
    EXPECT_EQ(mRegistry.enumerate().size(), 0);
}

TEST_F(CameraImageProviderRegistryTest, atBounds) {
    EXPECT_EQ(mRegistry.enumerate().size(), 2);
    EXPECT_NE(mRegistry[0], nullptr);
    EXPECT_NE(mRegistry[1], nullptr);
    EXPECT_EQ(mRegistry[2], nullptr);
    EXPECT_EQ(mRegistry[100], nullptr);
}

TEST_F(CameraImageProviderRegistryTest, atEmpty) {
    EXPECT_EQ(mRegistry.enumerate().size(), 2);
    EXPECT_NE(mRegistry[0], nullptr);

    CameraImageProviderInfoCpp steal(std::move(*mRegistry[0]));

    EXPECT_EQ(mRegistry[0], nullptr);
}
