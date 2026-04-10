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

#include "VirtualsceneImageProvider.h"

namespace goldfish::camera_image_providers::virtualscene {

namespace {
// Helper to compare CropRegions for more detailed test failures.
::testing::AssertionResult IsCropRegionEq(const VirtualsceneImageProvider::CropRegion& expected,
                                          const VirtualsceneImageProvider::CropRegion& actual) {
    if (expected.offset.width == actual.offset.width &&
        expected.offset.height == actual.offset.height &&
        expected.size.width == actual.size.width && expected.size.height == actual.size.height) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure()
           << "Expected crop region: offset=(" << expected.offset.width << ","
           << expected.offset.height << "), size=(" << expected.size.width << ","
           << expected.size.height << "). Actual: offset=(" << actual.offset.width << ","
           << actual.offset.height << "), size=(" << actual.size.width << "," << actual.size.height
           << ")";
}
}  // namespace

TEST(VirtualsceneImageProvider, GetCropRegion_SameAspectRatio) {
    const CameraImageProviderRect src = {1920, 1080};  // 16:9
    const CameraImageProviderRect dst = {1280, 720};   // 16:9
    const auto region = VirtualsceneImageProvider::getCropRegion(src, dst);
    const VirtualsceneImageProvider::CropRegion expected = {{0, 0}, {1920, 1080}};
    EXPECT_TRUE(IsCropRegionEq(expected, region));
}

TEST(VirtualsceneImageProvider, GetCropRegion_Pillarbox) {
    const CameraImageProviderRect src = {1920, 1080};  // 16:9
    const CameraImageProviderRect dst = {1024, 768};   // 4:3
    // src aspect (1.777) > dst aspect (1.333) -> pillarbox
    const auto region = VirtualsceneImageProvider::getCropRegion(src, dst);
    // new width = 1080 * (1024/768) = 1440
    // offset x = (1920 - 1440) / 2 = 240
    const VirtualsceneImageProvider::CropRegion expected = {{240, 0}, {1440, 1080}};
    EXPECT_TRUE(IsCropRegionEq(expected, region));
}

TEST(VirtualsceneImageProvider, GetCropRegion_Letterbox) {
    const CameraImageProviderRect src = {1024, 768};   // 4:3
    const CameraImageProviderRect dst = {1920, 1080};  // 16:9
    // src aspect (1.333) < dst aspect (1.777) -> letterbox
    const auto region = VirtualsceneImageProvider::getCropRegion(src, dst);
    // new height = 1024 / (1920/1080) = 576
    // offset y = (768 - 576) / 2 = 96
    const VirtualsceneImageProvider::CropRegion expected = {{0, 96}, {1024, 576}};
    EXPECT_TRUE(IsCropRegionEq(expected, region));
}

TEST(VirtualsceneImageProvider, GetCropRegion_SrcSmallerThanDst) {
    const CameraImageProviderRect src = {640, 480};    // 4:3
    const CameraImageProviderRect dst = {1920, 1080};  // 16:9
    // src aspect (1.333) < dst aspect (1.777) -> letterbox
    const auto region = VirtualsceneImageProvider::getCropRegion(src, dst);
    // new height = 640 / (1920/1080) = 360
    // offset y = (480 - 360) / 2 = 60
    const VirtualsceneImageProvider::CropRegion expected = {{0, 60}, {640, 360}};
    EXPECT_TRUE(IsCropRegionEq(expected, region));
}

TEST(VirtualsceneImageProvider, FindCaptureStreamBuffers) {
    // Setup: create a vector of buffers to search through.
    std::vector<VirtualsceneImageProvider::CaptureStreamBuffers> bufs(3);
    bufs[0].id = 10;
    bufs[1].id = 20;
    bufs[2].id = 30;

    // Test case: Find an existing buffer in the middle.
    const auto* found =
            VirtualsceneImageProvider::findCaptureStreamBuffers(20, bufs.data(), bufs.size());
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, 20);
    EXPECT_EQ(found, &bufs[1]);

    // Test case: Search for a non-existent buffer.
    EXPECT_EQ(VirtualsceneImageProvider::findCaptureStreamBuffers(99, bufs.data(), bufs.size()),
              nullptr);

    // Test case: Find the first and last buffers.
    EXPECT_EQ(VirtualsceneImageProvider::findCaptureStreamBuffers(10, bufs.data(), bufs.size()),
              &bufs[0]);
    EXPECT_EQ(VirtualsceneImageProvider::findCaptureStreamBuffers(30, bufs.data(), bufs.size()),
              &bufs[2]);

    // Test case: Search in an empty array.
    EXPECT_EQ(VirtualsceneImageProvider::findCaptureStreamBuffers(10, nullptr, 0), nullptr);
}

}  // namespace goldfish::camera_image_providers::virtualscene
