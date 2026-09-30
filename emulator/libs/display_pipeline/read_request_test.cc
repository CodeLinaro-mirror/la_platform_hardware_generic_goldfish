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
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/barrier.h"

#include "goldfish/display/frame.h"
#include "goldfish/display/geometry.h"

namespace goldfish::display {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::Each;

constexpr uint32_t kMax32 = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kMaxInt = std::numeric_limits<int>::max();
constexpr Dimensions kOddFrame{37, 23};
constexpr uint8_t kSentinel = 0xAB;

// Records the resolved ReadRequest that Slot::ReadInto hands to DoReadInto.
class RecordingSlot final : public Slot {
  public:
    explicit RecordingSlot(Dimensions size) : size_(size) {}
    Dimensions size() const override { return size_; }
    const std::optional<ReadRequest>& Resolved() const { return resolved_; }

  protected:
    absl::Status DoReadInto(const ReadRequest& read, const ReadTarget& /*dst*/) const override {
        resolved_ = read;
        return absl::OkStatus();
    }

  private:
    const Dimensions size_;
    mutable std::optional<ReadRequest> resolved_;
};

absl::StatusOr<ReadRequest> ResolveRead(Dimensions frame, const ReadRequest& request,
                                        const ReadTarget& target) {
    RecordingSlot slot(frame);
    absl::Status status = slot.ReadInto(request, target);
    if (!status.ok()) return status;
    return *slot.Resolved();
}

// Owns destination memory and hands out targets over it.
class TargetBuffer {
  public:
    explicit TargetBuffer(size_t bytes) : bytes_(bytes) {}

    Plane PlaneOf(size_t size, size_t stride = 0) {
        return Plane{absl::MakeSpan(bytes_.data(), size), stride};
    }

  private:
    std::vector<uint8_t> bytes_;
};

// A target of `format` whose planes are exactly large enough for `layout`.
struct SizedTarget {
    SizedTarget(const ReadLayout& layout, ReadFormat format) : buffer(layout.total_bytes + 1) {
        switch (format) {
        case ReadFormat::kRgba8888:
            target = Rgba8888Target{buffer.PlaneOf(layout.planes[0].bytes)};
            break;
        case ReadFormat::kRgb888:
            target = Rgb888Target{buffer.PlaneOf(layout.planes[0].bytes)};
            break;
        case ReadFormat::kNv12:
            target = Nv12Target{buffer.PlaneOf(layout.planes[0].bytes),
                                buffer.PlaneOf(layout.planes[1].bytes)};
            break;
        }
    }
    TargetBuffer buffer;
    ReadTarget target;
};

// An exact-size heap allocation of `rows` rows of `stride` bytes, prefilled
// with kSentinel, so ASAN flags any write past the end.
class HeapPlane {
  public:
    HeapPlane(size_t stride, size_t rows)
            : stride_(stride), rows_(rows), bytes_(new uint8_t[stride * rows]) {
        std::fill_n(bytes_.get(), stride * rows, kSentinel);
    }

    Plane AsPlane() const { return Plane{absl::MakeSpan(bytes_.get(), stride_ * rows_), stride_}; }

    // Expects every row to start with `row_bytes` bytes of the repeating
    // `pattern`, and the padding after them to still hold kSentinel.
    void ExpectRows(size_t row_bytes, std::initializer_list<uint8_t> pattern) const {
        const std::vector<uint8_t> expected(pattern);
        for (size_t y = 0; y < rows_; ++y) {
            const uint8_t* row = bytes_.get() + y * stride_;
            for (size_t x = 0; x < stride_; ++x) {
                const uint8_t want = x < row_bytes ? expected[x % expected.size()] : kSentinel;
                ASSERT_EQ(row[x], want) << "row " << y << " byte " << x;
            }
        }
    }

  private:
    const size_t stride_;
    const size_t rows_;
    const std::unique_ptr<uint8_t[]> bytes_;
};

constexpr std::array kAllFormats = {ReadFormat::kRgba8888, ReadFormat::kRgb888, ReadFormat::kNv12};

ReadFormat FormatOfTarget(const ReadTarget& target) {
    return std::visit([](const auto& t) { return FormatForTarget<std::decay_t<decltype(t)>>(); },
                      target);
}

std::vector<Plane> PlanesOf(const ReadTarget& target) {
    if (const auto* nv12 = std::get_if<Nv12Target>(&target)) return {nv12->y, nv12->uv};
    if (const auto* rgb = std::get_if<Rgb888Target>(&target)) return {rgb->plane};
    return {std::get<Rgba8888Target>(target).plane};
}

// True if `bytes` is exactly a tightly packed opaque-black image for `layout`.
bool IsOpaqueBlack(const ReadLayout& layout, const std::vector<uint8_t>& bytes) {
    if (layout.total_bytes == 0 || bytes.size() != layout.total_bytes) return false;
    for (size_t i = 0; i < bytes.size(); ++i) {
        uint8_t want = 0;
        switch (layout.format) {
        case ReadFormat::kRgba8888:
            want = i % 4 == 3 ? 0xFF : 0x00;
            break;
        case ReadFormat::kRgb888:
            want = 0x00;
            break;
        case ReadFormat::kNv12:
            want = i < layout.planes[0].bytes ? 16 : 128;
            break;
        }
        if (bytes[i] != want) return false;
    }
    return true;
}

// A large RGBA target that fits any read of a small frame.
class ResolveReadTest : public ::testing::Test {
  protected:
    ResolveReadTest() : buffer_(1 << 20) {}

    ReadTarget BigRgba() { return Rgba8888Target{buffer_.PlaneOf(1 << 20)}; }
    ReadTarget BigNv12() { return Nv12Target{buffer_.PlaneOf(1 << 19), buffer_.PlaneOf(1 << 19)}; }

    TargetBuffer buffer_;
};

ReadRequest Resolved(Rect crop, ImageRotation rotation, Dimensions output) {
    return ReadRequest{.crop = crop, .rotation = rotation, .output = output};
}

TEST_F(ResolveReadTest, WholeFrameByDefault) {
    EXPECT_THAT(ResolveRead(kOddFrame, {}, BigRgba()),
                IsOkAndHolds(Resolved({0, 0, 37, 23}, ImageRotation::kRotation0, {37, 23})));
}

TEST_F(ResolveReadTest, QuarterTurnsSwapTheDefaultOutput) {
    for (ImageRotation rotation : {ImageRotation::kRotation90, ImageRotation::kRotation270}) {
        EXPECT_THAT(ResolveRead(kOddFrame, {.rotation = rotation}, BigRgba()),
                    IsOkAndHolds(Resolved({0, 0, 37, 23}, rotation, {23, 37})));
    }
    EXPECT_THAT(ResolveRead(kOddFrame, {.rotation = ImageRotation::kRotation180}, BigRgba()),
                IsOkAndHolds(Resolved({0, 0, 37, 23}, ImageRotation::kRotation180, {37, 23})));
}

TEST_F(ResolveReadTest, CropSetsTheDefaultOutput) {
    const Rect crop{5, 6, 10, 7};
    EXPECT_THAT(ResolveRead(kOddFrame, {.crop = crop}, BigRgba()),
                IsOkAndHolds(Resolved(crop, ImageRotation::kRotation0, {10, 7})));
    EXPECT_THAT(ResolveRead(kOddFrame, {.crop = crop, .rotation = ImageRotation::kRotation90},
                            BigRgba()),
                IsOkAndHolds(Resolved(crop, ImageRotation::kRotation90, {7, 10})));
}

TEST_F(ResolveReadTest, CropMayTouchEveryEdge) {
    EXPECT_THAT(ResolveRead(kOddFrame, {.crop = Rect{29, 15, 8, 8}}, BigRgba()), IsOk());
    EXPECT_THAT(ResolveRead(kOddFrame, {.crop = Rect{0, 0, 1, 1}}, BigRgba()), IsOk());
}

TEST_F(ResolveReadTest, ExplicitOutputIsKept) {
    EXPECT_THAT(ResolveRead(kOddFrame, {.output = {21, 13}}, BigRgba()),
                IsOkAndHolds(Resolved({0, 0, 37, 23}, ImageRotation::kRotation0, {21, 13})));
}

TEST_F(ResolveReadTest, RejectsEmptyCrop) {
    EXPECT_THAT(ResolveRead(kOddFrame, {.crop = Rect{0, 0, 0, 5}}, BigRgba()),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(ResolveRead(kOddFrame, {.crop = Rect{0, 0, 5, 0}}, BigRgba()),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ResolveReadTest, RejectsCropOutsideTheFrame) {
    EXPECT_THAT(ResolveRead(kOddFrame, {.crop = Rect{30, 0, 8, 1}}, BigRgba()),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(ResolveRead(kOddFrame, {.crop = Rect{0, 16, 1, 8}}, BigRgba()),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(ResolveRead(kOddFrame, {.crop = Rect{0, 0, 38, 1}}, BigRgba()),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ResolveReadTest, RejectsCropThatWrapsIn32Bits) {
    // x + width wraps to 1 in 32-bit arithmetic.
    EXPECT_THAT(ResolveRead(kOddFrame, {.crop = Rect{kMax32, 0, 2, 1}}, BigRgba()),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(ResolveRead(kOddFrame, {.crop = Rect{0, kMax32, 1, 2}}, BigRgba()),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(ResolveRead(kOddFrame, {.crop = Rect{1, 0, kMax32, 1}}, BigRgba()),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ResolveReadTest, RejectsOutputWithOneZeroDimension) {
    EXPECT_THAT(ResolveRead(kOddFrame, {.output = {10, 0}}, BigRgba()),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(ResolveRead(kOddFrame, {.output = {0, 10}}, BigRgba()),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ResolveReadTest, RejectsUpscaling) {
    EXPECT_THAT(ResolveRead(kOddFrame, {.output = {38, 23}}, BigRgba()),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(ResolveRead(kOddFrame, {.output = {37, 24}}, BigRgba()),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(ResolveRead(kOddFrame, {.output = {kMax32, kMax32}}, BigRgba()),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ResolveReadTest, OutputIsComparedInTheRotatedOrientation) {
    EXPECT_THAT(ResolveRead(kOddFrame, {.rotation = ImageRotation::kRotation90, .output = {37, 23}},
                            BigRgba()),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(ResolveRead(kOddFrame, {.rotation = ImageRotation::kRotation90, .output = {23, 37}},
                            BigRgba()),
                IsOk());
}

TEST_F(ResolveReadTest, RejectsAnyReadOfAnEmptyFrame) {
    EXPECT_THAT(ResolveRead({0, 0}, {}, BigRgba()), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ResolveReadTest, Nv12DropsTheLastOddColumnAndRow) {
    EXPECT_THAT(ResolveRead(kOddFrame, {}, BigNv12()),
                IsOkAndHolds(Resolved({0, 0, 36, 22}, ImageRotation::kRotation0, {36, 22})));
    EXPECT_THAT(ResolveRead(kOddFrame, {.crop = Rect{1, 1, 5, 3}}, BigNv12()),
                IsOkAndHolds(Resolved({1, 1, 4, 2}, ImageRotation::kRotation0, {4, 2})));
}

TEST_F(ResolveReadTest, Nv12DropsBeforeRotating) {
    EXPECT_THAT(ResolveRead(kOddFrame,
                            {.crop = Rect{0, 0, 5, 3}, .rotation = ImageRotation::kRotation90},
                            BigNv12()),
                IsOkAndHolds(Resolved({0, 0, 4, 2}, ImageRotation::kRotation90, {2, 4})));
}

TEST_F(ResolveReadTest, Nv12RoundsAnExplicitOutputDownToEven) {
    // The crop keeps its odd size: the scale step resamples instead.
    EXPECT_THAT(ResolveRead(kOddFrame, {.output = {21, 13}}, BigNv12()),
                IsOkAndHolds(Resolved({0, 0, 37, 23}, ImageRotation::kRotation0, {20, 12})));
}

TEST_F(ResolveReadTest, Nv12ExplicitOutputIsAtLeastTwo) {
    EXPECT_THAT(ResolveRead(kOddFrame, {.output = {1, 1}}, BigNv12()),
                IsOkAndHolds(Resolved({0, 0, 37, 23}, ImageRotation::kRotation0, {2, 2})));
}

TEST_F(ResolveReadTest, Nv12RejectsCropSmallerThanTwoByTwo) {
    EXPECT_THAT(ResolveRead(kOddFrame, {.crop = Rect{0, 0, 1, 5}}, BigNv12()),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(ResolveRead(kOddFrame, {.crop = Rect{0, 0, 5, 1}}, BigNv12()),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(ResolveRead({1, 9}, {}, BigNv12()), StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(ResolveRead(kOddFrame, {.crop = Rect{0, 0, 2, 2}}, BigNv12()), IsOk());
}

TEST_F(ResolveReadTest, Nv12RoundsAnExplicitOutputAfterAQuarterTurn) {
    // The output is in rotated space: 23 wide, 37 tall.
    EXPECT_THAT(ResolveRead(kOddFrame, {.rotation = ImageRotation::kRotation90, .output = {23, 37}},
                            BigNv12()),
                IsOkAndHolds(Resolved({0, 0, 37, 23}, ImageRotation::kRotation90, {22, 36})));
    EXPECT_THAT(
            ResolveRead(kOddFrame, {.rotation = ImageRotation::kRotation270, .output = {21, 35}},
                        BigNv12()),
            IsOkAndHolds(Resolved({0, 0, 37, 23}, ImageRotation::kRotation270, {20, 34})));
    EXPECT_THAT(ResolveRead(kOddFrame, {.rotation = ImageRotation::kRotation90, .output = {24, 37}},
                            BigNv12()),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ResolveReadTest, RejectsNonOrthogonalRotation) {
    for (int degrees : {1, 45, 360}) {
        const ReadRequest request{.rotation = static_cast<ImageRotation>(degrees)};
        EXPECT_THAT(ResolveRead(kOddFrame, request, BigRgba()),
                    StatusIs(absl::StatusCode::kInvalidArgument))
                << degrees;
        EXPECT_EQ(Slot::LayoutFor(kOddFrame, request, ReadFormat::kRgba8888).total_bytes, 0U)
                << degrees;
    }
}

TEST_F(ResolveReadTest, RejectsFramesLibyuvCannotAddress) {
    constexpr uint32_t kTooBig = uint32_t{kMaxInt} + 1;
    EXPECT_THAT(ResolveRead({kTooBig, 2}, {.crop = Rect{0, 0, 2, 2}}, BigRgba()),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(ResolveRead({2, kTooBig}, {.crop = Rect{0, 0, 2, 2}}, BigRgba()),
                StatusIs(absl::StatusCode::kInvalidArgument));
    // One byte per pixel at the limit is still addressable.
    EXPECT_EQ(Slot::LayoutFor({kMaxInt, 2}, {}, ReadFormat::kNv12).dimensions,
              (Dimensions{kMaxInt - 1, 2}));
    EXPECT_EQ(Slot::LayoutFor({kTooBig, 2}, {}, ReadFormat::kNv12).dimensions, (Dimensions{0, 0}));
}

TEST(ResolveReadPlaneTest, AcceptsExactTightPlanes) {
    for (ReadFormat format : {ReadFormat::kRgba8888, ReadFormat::kRgb888, ReadFormat::kNv12}) {
        const ReadLayout layout = Slot::LayoutFor(kOddFrame, {}, format);
        SizedTarget sized(layout, format);
        EXPECT_THAT(ResolveRead(kOddFrame, {}, sized.target), IsOk()) << static_cast<int>(format);
    }
}

TEST(ResolveReadPlaneTest, RejectsTightPlaneOneByteShort) {
    TargetBuffer buffer(size_t{37} * 23 * 4);
    EXPECT_THAT(ResolveRead(kOddFrame, {}, Rgba8888Target{buffer.PlaneOf(37 * 23 * 4 - 1)}),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(ResolveRead(kOddFrame, {}, Rgb888Target{buffer.PlaneOf(37 * 23 * 3 - 1)}),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ResolveReadPlaneTest, PaddedStrideNeedsStrideTimesRows) {
    constexpr size_t kStride = 37 * 4 + 12;
    TargetBuffer buffer(kStride * 23);
    EXPECT_THAT(ResolveRead(kOddFrame, {}, Rgba8888Target{buffer.PlaneOf(kStride * 23, kStride)}),
                IsOk());
    EXPECT_THAT(
            ResolveRead(kOddFrame, {}, Rgba8888Target{buffer.PlaneOf(kStride * 23 - 1, kStride)}),
            StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ResolveReadPlaneTest, RejectsStrideShorterThanOneRow) {
    TargetBuffer buffer(1 << 16);
    EXPECT_THAT(ResolveRead(kOddFrame, {}, Rgba8888Target{buffer.PlaneOf(1 << 16, 37 * 4 - 1)}),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(ResolveRead(kOddFrame, {}, Rgb888Target{buffer.PlaneOf(1 << 16, 37 * 3 - 1)}),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ResolveReadPlaneTest, RejectsStridesLibyuvCannotAddress) {
    // A span this large is never allocated; only its size is inspected.
    constexpr size_t kStride = size_t{std::numeric_limits<int>::max()} + 1;
    std::vector<uint8_t> one_byte(1);
    const Plane huge{absl::Span<uint8_t>(one_byte.data(), kStride * 23), kStride};
    EXPECT_THAT(ResolveRead(kOddFrame, {}, Rgba8888Target{huge}),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ResolveReadPlaneTest, ChecksBothNv12Planes) {
    // 36x22 after dropping the odd column and row.
    constexpr size_t kY = size_t{36} * 22;
    constexpr size_t kUv = size_t{36} * 11;
    TargetBuffer buffer(kY + kUv);
    EXPECT_THAT(ResolveRead(kOddFrame, {}, Nv12Target{buffer.PlaneOf(kY), buffer.PlaneOf(kUv)}),
                IsOk());
    EXPECT_THAT(ResolveRead(kOddFrame, {}, Nv12Target{buffer.PlaneOf(kY - 1), buffer.PlaneOf(kUv)}),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(ResolveRead(kOddFrame, {}, Nv12Target{buffer.PlaneOf(kY), buffer.PlaneOf(kUv - 1)}),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(ResolveRead(kOddFrame, {}, Nv12Target{buffer.PlaneOf(kY), buffer.PlaneOf(kUv, 35)}),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ResolveReadPlaneTest, RejectsEmptySpan) {
    EXPECT_THAT(ResolveRead(kOddFrame, {}, Rgba8888Target{}),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(LayoutForTest, Rgba) {
    const ReadLayout layout = Slot::LayoutFor(kOddFrame, {}, ReadFormat::kRgba8888);
    EXPECT_EQ(layout.dimensions, kOddFrame);
    EXPECT_EQ(layout.format, ReadFormat::kRgba8888);
    EXPECT_EQ(layout.plane_count, 1);
    EXPECT_EQ(layout.planes[0].offset, 0U);
    EXPECT_EQ(layout.planes[0].stride, 37U * 4);
    EXPECT_EQ(layout.planes[0].bytes, 37U * 4 * 23);
    EXPECT_EQ(layout.total_bytes, 37U * 4 * 23);
}

TEST(LayoutForTest, Rgb888Rotated) {
    const ReadLayout layout = Slot::LayoutFor(kOddFrame, {.rotation = ImageRotation::kRotation270},
                                              ReadFormat::kRgb888);
    EXPECT_EQ(layout.dimensions, (Dimensions{23, 37}));
    EXPECT_EQ(layout.plane_count, 1);
    EXPECT_EQ(layout.planes[0].stride, 23U * 3);
    EXPECT_EQ(layout.total_bytes, 23U * 3 * 37);
}

TEST(LayoutForTest, Nv12) {
    const ReadLayout layout = Slot::LayoutFor(kOddFrame, {}, ReadFormat::kNv12);
    EXPECT_EQ(layout.dimensions, (Dimensions{36, 22}));
    EXPECT_EQ(layout.format, ReadFormat::kNv12);
    EXPECT_EQ(layout.plane_count, 2);
    EXPECT_EQ(layout.planes[0].offset, 0U);
    EXPECT_EQ(layout.planes[0].stride, 36U);
    EXPECT_EQ(layout.planes[0].bytes, 36U * 22);
    EXPECT_EQ(layout.planes[1].offset, 36U * 22);
    EXPECT_EQ(layout.planes[1].stride, 36U);
    EXPECT_EQ(layout.planes[1].bytes, 36U * 11);
    EXPECT_EQ(layout.total_bytes, 36U * 22 + 36U * 11);
}

TEST(LayoutForTest, InvalidRequestHasNoSize) {
    for (ReadFormat format : kAllFormats) {
        const int plane_count = Slot::LayoutFor(kOddFrame, {}, format).plane_count;
        // Too large for every format; the tiny crop is invalid only for NV12.
        for (const ReadRequest& request :
             {ReadRequest{.output = {99, 99}}, ReadRequest{.crop = Rect{0, 0, 1, 1}}}) {
            if (request.crop.has_value() && format != ReadFormat::kNv12) continue;
            const ReadLayout layout = Slot::LayoutFor(kOddFrame, request, format);
            EXPECT_EQ(layout.dimensions, (Dimensions{0, 0}));
            EXPECT_EQ(layout.total_bytes, 0U);
            EXPECT_EQ(layout.format, format);
            EXPECT_EQ(layout.plane_count, plane_count);
            for (const PlaneLayout& plane : layout.planes) {
                EXPECT_EQ(plane.offset, 0U);
                EXPECT_EQ(plane.stride, 0U);
                EXPECT_EQ(plane.bytes, 0U);
            }
        }
    }
}

TEST(ReadLayoutTest, TargetForSlicesAContiguousBuffer) {
    const ReadLayout layout = Slot::LayoutFor({6, 4}, {}, ReadFormat::kNv12);
    std::vector<uint8_t> buffer(layout.total_bytes);
    const ReadTarget target = layout.TargetFor(buffer.data());
    const auto* nv12 = std::get_if<Nv12Target>(&target);
    ASSERT_NE(nv12, nullptr);
    EXPECT_EQ(nv12->y.data.data(), buffer.data());
    EXPECT_EQ(nv12->y.data.size(), 6U * 4);
    EXPECT_EQ(nv12->y.stride, 6U);
    EXPECT_EQ(nv12->uv.data.data(), buffer.data() + size_t{6} * 4);
    EXPECT_EQ(nv12->uv.data.size(), 6U * 2);
    EXPECT_EQ(nv12->uv.stride, 6U);
}

TEST(ReadLayoutTest, TargetForNullOrEmptyLayoutHasEmptyPlanes) {
    std::vector<uint8_t> buffer(1024);
    for (ReadFormat format : kAllFormats) {
        const ReadLayout valid = Slot::LayoutFor({6, 4}, {}, format);
        const ReadLayout invalid = Slot::LayoutFor({6, 4}, {.output = {99, 99}}, format);
        for (const ReadTarget& target :
             {valid.TargetFor(nullptr), invalid.TargetFor(buffer.data())}) {
            EXPECT_EQ(FormatOfTarget(target), format);
            const std::vector<Plane> planes = PlanesOf(target);
            EXPECT_EQ(planes.size(), static_cast<size_t>(valid.plane_count));
            for (const Plane& plane : planes) {
                EXPECT_TRUE(plane.data.empty());
            }
        }
    }
}

TEST(BlankSlotTest, WritesBlackInEveryFormat) {
    const std::unique_ptr<const Slot> blank = Slot::Blank({6, 4});

    std::vector<uint8_t> rgba(size_t{6} * 4 * 4, 0x55);
    ASSERT_THAT(blank->ReadInto({}, Rgba8888Target{Plane{absl::MakeSpan(rgba)}}), IsOk());
    for (size_t i = 0; i < rgba.size(); ++i) {
        EXPECT_EQ(rgba[i], i % 4 == 3 ? 0xFF : 0x00) << "byte " << i;
    }

    std::vector<uint8_t> rgb(size_t{6} * 4 * 3, 0x55);
    ASSERT_THAT(blank->ReadInto({}, Rgb888Target{Plane{absl::MakeSpan(rgb)}}), IsOk());
    EXPECT_THAT(rgb, Each(0x00));

    std::vector<uint8_t> y(size_t{6} * 4, 0x55);
    std::vector<uint8_t> uv(size_t{6} * 2, 0x55);
    ASSERT_THAT(
            blank->ReadInto({}, Nv12Target{Plane{absl::MakeSpan(y)}, Plane{absl::MakeSpan(uv)}}),
            IsOk());
    EXPECT_THAT(y, Each(16));
    EXPECT_THAT(uv, Each(128));
}

TEST(BlankSlotTest, LeavesRowPaddingUntouched) {
    // Odd strides also make rows start at unaligned addresses.
    const std::unique_ptr<const Slot> blank = Slot::Blank({7, 5});

    HeapPlane rgba(7 * 4 + 3, 5);
    ASSERT_THAT(blank->ReadInto({}, Rgba8888Target{rgba.AsPlane()}), IsOk());
    rgba.ExpectRows(size_t{7} * 4, {0, 0, 0, 0xFF});

    HeapPlane rgb(7 * 3 + 5, 5);
    ASSERT_THAT(blank->ReadInto({}, Rgb888Target{rgb.AsPlane()}), IsOk());
    rgb.ExpectRows(size_t{7} * 3, {0});

    // NV12 drops the odd column and row: 6x4.
    HeapPlane y(6 + 3, 4);
    HeapPlane uv(6 + 5, 2);
    ASSERT_THAT(blank->ReadInto({}, Nv12Target{y.AsPlane(), uv.AsPlane()}), IsOk());
    y.ExpectRows(6, {16});
    uv.ExpectRows(6, {128});
}

TEST(BlankSlotTest, RejectsTooSmallPlane) {
    const std::unique_ptr<const Slot> blank = Slot::Blank({6, 4});
    std::vector<uint8_t> bytes(6 * 4 * 4 - 1);
    EXPECT_THAT(blank->ReadInto({}, Rgba8888Target{Plane{absl::MakeSpan(bytes)}}),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(BlankSlotTest, ConcurrentReadsAreSafe) {
    constexpr int kThreads = 8;
    constexpr int kIterations = 200;
    constexpr std::array kRotations = {ImageRotation::kRotation0, ImageRotation::kRotation90,
                                       ImageRotation::kRotation180, ImageRotation::kRotation270};
    const std::unique_ptr<const Slot> blank = Slot::Blank(kOddFrame);
    absl::Barrier start(kThreads);
    std::atomic<int> black_reads{0};

    std::vector<std::thread> readers;
    readers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        readers.emplace_back([&, t] {
            std::vector<uint8_t> bytes;
            start.Block();
            for (int i = 0; i < kIterations; ++i) {
                const ReadFormat format = kAllFormats[(t + i) % kAllFormats.size()];
                const ReadRequest request{.rotation = kRotations[i % kRotations.size()],
                                          .output = i % 2 == 0 ? Dimensions{} : Dimensions{8, 8}};
                ReadLayout seen;
                const absl::StatusOr<Dimensions> dims = blank->ReadInto(
                        request, format, [&](const ReadLayout& layout) -> absl::StatusOr<uint8_t*> {
                            seen = layout;
                            bytes.assign(layout.total_bytes, kSentinel);
                            return bytes.data();
                        });
                if (dims.ok() && IsOpaqueBlack(seen, bytes)) black_reads.fetch_add(1);
            }
        });
    }
    for (std::thread& reader : readers) reader.join();
    EXPECT_EQ(black_reads.load(), kThreads * kIterations);
}

TEST(ReadIntoAllocatorTest, DeducesTargetFormatAtCompileTime) {
    const std::unique_ptr<const Slot> blank = Slot::Blank(kOddFrame);

    std::vector<uint8_t> rgba;
    EXPECT_THAT(blank->ReadInto({.rotation = ImageRotation::kRotation90},
                                [&](const ReadLayout& layout) -> absl::StatusOr<Rgba8888Target> {
                                    EXPECT_EQ(layout.format, ReadFormat::kRgba8888);
                                    EXPECT_EQ(layout.dimensions, (Dimensions{23, 37}));
                                    rgba.assign(layout.total_bytes, 0x55);
                                    return Rgba8888Target{Plane{absl::MakeSpan(rgba)}};
                                }),
                IsOkAndHolds(Dimensions{23, 37}));
    EXPECT_EQ(rgba.size(), 23U * 37U * 4U);

    std::vector<uint8_t> y;
    std::vector<uint8_t> uv;
    EXPECT_THAT(blank->ReadInto({},
                                [&](const ReadLayout& layout) -> absl::StatusOr<Nv12Target> {
                                    EXPECT_EQ(layout.format, ReadFormat::kNv12);
                                    EXPECT_EQ(layout.dimensions, (Dimensions{36, 22}));
                                    y.assign(layout.planes[0].bytes, 0x00);
                                    uv.assign(layout.planes[1].bytes, 0x00);
                                    return Nv12Target{Plane{absl::MakeSpan(y)},
                                                      Plane{absl::MakeSpan(uv)}};
                                }),
                IsOkAndHolds(Dimensions{36, 22}));
    EXPECT_THAT(y, Each(16));
    EXPECT_THAT(uv, Each(128));
}

TEST(ReadIntoAllocatorTest, ContiguousByteAllocatorSlicesPlanes) {
    const std::unique_ptr<const Slot> blank = Slot::Blank({6, 4});
    std::vector<uint8_t> nv12_buf;
    EXPECT_THAT(blank->ReadInto({}, ReadFormat::kNv12,
                                [&](const ReadLayout& layout) -> absl::StatusOr<uint8_t*> {
                                    nv12_buf.assign(layout.total_bytes, 0x00);
                                    return nv12_buf.data();
                                }),
                IsOkAndHolds(Dimensions{6, 4}));
    ASSERT_EQ(nv12_buf.size(), 6U * 4U + 6U * 2U);
    EXPECT_THAT(absl::MakeSpan(nv12_buf).subspan(0, 24), Each(16));
    EXPECT_THAT(absl::MakeSpan(nv12_buf).subspan(24, 12), Each(128));
}

TEST(ReadIntoAllocatorTest, DoesNotInvokeAllocatorWhenGeometryIsInvalid) {
    const std::unique_ptr<const Slot> blank = Slot::Blank(kOddFrame);
    bool called = false;
    EXPECT_THAT(blank->ReadInto({.output = {99, 99}},
                                [&](const ReadLayout&) -> absl::StatusOr<Rgba8888Target> {
                                    called = true;
                                    return Rgba8888Target{};
                                }),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_FALSE(called);
}

TEST(ReadIntoAllocatorTest, PropagatesAllocatorError) {
    const std::unique_ptr<const Slot> blank = Slot::Blank(kOddFrame);
    EXPECT_THAT(blank->ReadInto({}, ReadFormat::kRgb888,
                                [](const ReadLayout&) -> absl::StatusOr<uint8_t*> {
                                    return absl::ResourceExhaustedError("pool full");
                                }),
                StatusIs(absl::StatusCode::kResourceExhausted));
}

TEST(ReadIntoAllocatorTest, NotCalledWhenRowBytesExceedLibyuvLimit) {
    // Each frame fits libyuv's int limits, but one output row does not.
    constexpr uint32_t kRgbaMax = kMaxInt / 4;
    constexpr uint32_t kRgbMax = kMaxInt / 3;
    struct Case {
        Dimensions frame;
        ReadFormat format;
    };
    for (const Case& c : {Case{{kRgbaMax + 1, 1}, ReadFormat::kRgba8888},
                          Case{{600'000'000, 1}, ReadFormat::kRgba8888},
                          Case{{kRgbMax + 1, 1}, ReadFormat::kRgb888}}) {
        const std::unique_ptr<const Slot> blank = Slot::Blank(c.frame);
        int calls = 0;
        EXPECT_THAT(blank->ReadInto({}, c.format,
                                    [&](const ReadLayout&) -> absl::StatusOr<uint8_t*> {
                                        ++calls;
                                        return absl::InternalError("must not allocate");
                                    }),
                    StatusIs(absl::StatusCode::kInvalidArgument));
        EXPECT_EQ(calls, 0) << c.frame.width;

        const ReadLayout layout = Slot::LayoutFor(c.frame, {}, c.format);
        EXPECT_EQ(layout.dimensions, (Dimensions{0, 0}));
        EXPECT_EQ(layout.total_bytes, 0U);
        EXPECT_EQ(layout.planes[0].stride, 0U);
    }

    const std::unique_ptr<const Slot> wide = Slot::Blank({kRgbaMax + 1, 1});
    int typed_calls = 0;
    EXPECT_THAT(wide->ReadInto({},
                               [&](const ReadLayout&) -> absl::StatusOr<Rgba8888Target> {
                                   ++typed_calls;
                                   return absl::InternalError("must not allocate");
                               }),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_EQ(typed_calls, 0);

    // At the limit the layout is still computed, and downscaling makes rows addressable.
    EXPECT_EQ(Slot::LayoutFor({kRgbaMax, 1}, {}, ReadFormat::kRgba8888).planes[0].stride,
              size_t{kRgbaMax} * 4);
    EXPECT_EQ(Slot::LayoutFor({kRgbMax, 1}, {}, ReadFormat::kRgb888).planes[0].stride,
              size_t{kRgbMax} * 3);
    EXPECT_EQ(Slot::LayoutFor({kRgbaMax + 1, 1}, {.output = {1000, 1}}, ReadFormat::kRgba8888)
                      .dimensions,
              (Dimensions{1000, 1}));
}

TEST(ReadIntoAllocatorTest, RejectsNullFromByteAllocator) {
    const RecordingSlot slot(kOddFrame);
    EXPECT_THAT(
            slot.ReadInto({}, ReadFormat::kNv12,
                          [](const ReadLayout&) -> absl::StatusOr<uint8_t*> { return nullptr; }),
            StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_FALSE(slot.Resolved().has_value());
}

TEST(ReadIntoAllocatorTest, RejectsShortSpanFromTypedAllocator) {
    const RecordingSlot slot(kOddFrame);
    // 36x22 after NV12 rounding; the UV plane is one byte short.
    std::vector<uint8_t> y(36 * 22);
    std::vector<uint8_t> uv(36 * 11 - 1);
    EXPECT_THAT(slot.ReadInto({},
                              [&](const ReadLayout&) -> absl::StatusOr<Nv12Target> {
                                  return Nv12Target{Plane{absl::MakeSpan(y)},
                                                    Plane{absl::MakeSpan(uv)}};
                              }),
                StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_FALSE(slot.Resolved().has_value());
}

TEST(ReadIntoAllocatorTest, TypedAllocatorMayPadStrides) {
    const std::unique_ptr<const Slot> blank = Slot::Blank(kOddFrame);
    std::optional<HeapPlane> y;
    std::optional<HeapPlane> uv;
    EXPECT_THAT(blank->ReadInto(
                        {.rotation = ImageRotation::kRotation90},
                        [&](const ReadLayout& layout) -> absl::StatusOr<Nv12Target> {
                            y.emplace(layout.planes[0].stride + 10, layout.dimensions.height);
                            uv.emplace(layout.planes[1].stride + 10, layout.dimensions.height / 2);
                            return Nv12Target{y->AsPlane(), uv->AsPlane()};
                        }),
                IsOkAndHolds(Dimensions{22, 36}));
    ASSERT_TRUE(y.has_value() && uv.has_value());
    y->ExpectRows(22, {16});
    uv->ExpectRows(22, {128});
}

TEST(ReadIntoAllocatorTest, PropagatesTypedAllocatorError) {
    const RecordingSlot slot(kOddFrame);
    EXPECT_THAT(slot.ReadInto({},
                              [](const ReadLayout&) -> absl::StatusOr<Rgb888Target> {
                                  return absl::ResourceExhaustedError("pool full");
                              }),
                StatusIs(absl::StatusCode::kResourceExhausted));
    EXPECT_FALSE(slot.Resolved().has_value());
}

}  // namespace
}  // namespace goldfish::display
