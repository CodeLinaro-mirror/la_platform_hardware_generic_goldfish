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
#include "goldfish/display/frame.h"

#include <gtest/gtest.h>

#include "absl/time/time.h"

#include "goldfish/display/geometry.h"
#include "goldfish/physics/skin_rotation.h"

namespace goldfish::display {
namespace {

TEST(FrameEventTest, OrdersAndComparesByFrameIdOnly) {
    const FrameEvent older{.frame_id = 3, .dimensions = {10, 20}};
    const FrameEvent newer{.frame_id = 4, .dimensions = {10, 20}};
    const FrameEvent same_id{.frame_id = 3, .dimensions = {30, 40}, .blank = true};

    EXPECT_LT(older, newer);
    EXPECT_GT(newer, older);
    EXPECT_EQ(older, same_id);
    EXPECT_NE(older, newer);
}

TEST(FrameEventTest, GeometryChangeComparesDimensions) {
    const FrameEvent a{.frame_id = 1, .dimensions = {10, 20}};
    const FrameEvent same{.frame_id = 2, .dimensions = {10, 20}};
    const FrameEvent wider{.frame_id = 3, .dimensions = {11, 20}};
    const FrameEvent taller{.frame_id = 4, .dimensions = {10, 21}};

    EXPECT_FALSE(same.HasGeometryChangeFrom(a));
    EXPECT_TRUE(wider.HasGeometryChangeFrom(a));
    EXPECT_TRUE(taller.HasGeometryChangeFrom(a));
}

TEST(FrameEventTest, GeometryChangeComparesRotation) {
    const FrameEvent a{.frame_id = 1, .dimensions = {10, 20}};
    const FrameEvent same_frame_rotated{
        .frame_id = 1, .dimensions = {10, 20}, .rotation = ImageRotation::kRotation90};
    const FrameEvent blank_same_geometry{.frame_id = 2, .dimensions = {10, 20}, .blank = true};

    EXPECT_TRUE(same_frame_rotated.HasGeometryChangeFrom(a));
    EXPECT_TRUE(a.HasGeometryChangeFrom(same_frame_rotated));
    // Blankness and identity are not geometry.
    EXPECT_FALSE(blank_same_geometry.HasGeometryChangeFrom(a));
}

TEST(FrameEventTest, DefaultsDescribeNoFrame) {
    const FrameEvent event;
    EXPECT_EQ(event.frame_id, 0U);
    EXPECT_EQ(event.dimensions, (Dimensions{0, 0}));
    EXPECT_FALSE(event.blank);
    EXPECT_EQ(event.capture_time, absl::InfinitePast());
}

TEST(ReadRequestTest, DefaultsReadWholeFrameUnrotatedAtNativeSize) {
    const ReadRequest request;
    EXPECT_FALSE(request.crop.has_value());
    EXPECT_EQ(request.rotation, ImageRotation::kRotation0);
    EXPECT_EQ(request.output, (Dimensions{0, 0}));
    EXPECT_EQ(request.yuv_matrix, YuvMatrix::kBt601Limited);
}

TEST(ImageRotationTest, SkinRotationMapsToCounterClockwiseQuarterTurns) {
    using physics::SkinRotation;
    static_assert(ToImageRotation(SkinRotation::kPortrait) == ImageRotation::kRotation0);
    EXPECT_EQ(ToImageRotation(SkinRotation::kPortrait), ImageRotation::kRotation0);
    EXPECT_EQ(ToImageRotation(SkinRotation::kLandscape), ImageRotation::kRotation90);
    EXPECT_EQ(ToImageRotation(SkinRotation::kReversePortrait), ImageRotation::kRotation180);
    EXPECT_EQ(ToImageRotation(SkinRotation::kReverseLandscape), ImageRotation::kRotation270);
}

}  // namespace
}  // namespace goldfish::display
