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
#include "goldfish/display/cpu_slot_storage.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <ios>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

extern "C" {
#include "pixman.h"
}

#include "goldfish/display/frame.h"
#include "goldfish/display/frame_source.h"
#include "goldfish/display/geometry.h"
#include "goldfish/display/legacy_surface_source.h"
#include "goldfish/display/slot_storage.h"
#include "goldfish/display/test/read_pipeline_conformance.h"

namespace goldfish::display {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::goldfish::display::conformance::Destination;
using ::goldfish::display::conformance::kScaledTolerance;
using ::goldfish::display::conformance::MakeNoiseFrame;
using ::goldfish::display::conformance::MaxAbsDiff;
using ::goldfish::display::conformance::ReadOutput;
using ::goldfish::display::conformance::ReferenceRead;
using ::goldfish::display::conformance::TestFrame;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;

constexpr Dimensions kSize{13, 7};

SurfaceSource SurfaceOf(const TestFrame& frame, uint32_t format) {
    const SlotPixels pixels = frame.pixels();
    return SurfaceSource{.data = pixels.data,
                         .dimensions = pixels.dimensions,
                         .stride = pixels.stride,
                         .pixman_format = format};
}

ReadRequest FullRead(Dimensions size, ImageRotation rotation = ImageRotation::kRotation0) {
    const bool quarter =
            rotation == ImageRotation::kRotation90 || rotation == ImageRotation::kRotation270;
    return ReadRequest{.crop = Rect{0, 0, size.width, size.height},
                       .rotation = rotation,
                       .output = quarter ? Dimensions{size.height, size.width} : size};
}

class CpuSlotStorageTest : public ::testing::Test {
  protected:
    // Captures `src` into a new slot of `slot_size`.
    // @return The capture result; done must have run inline.
    CaptureResult CaptureInto(const SurfaceSource& src, Dimensions slot_size) {
        absl::StatusOr<std::unique_ptr<Slot>> buffer = storage_.Allocate(slot_size);
        EXPECT_THAT(buffer, IsOk());
        if (!buffer.ok()) return CaptureResult{.status = buffer.status()};
        slot_ = *std::move(buffer);
        std::optional<CaptureResult> result;
        storage_.Capture(*slot_, src, [&](CaptureResult r) { result = std::move(r); });
        EXPECT_TRUE(result.has_value()) << "done must run inline";
        return result.value_or(CaptureResult{.status = absl::InternalError("no result")});
    }

    ReadOutput ReadSlot(const ReadRequest& read, ReadFormat format) {
        Destination dst(read.output, format, /*row_padding=*/3);
        EXPECT_THAT(slot_->ReadInto(read, dst.target()), IsOk());
        EXPECT_TRUE(dst.PaddingIntact());
        return dst.Pixels();
    }

    CpuSlotStorage storage_;
    std::unique_ptr<Slot> slot_;
};

TEST_F(CpuSlotStorageTest, AllocateRejectsEmptyAndOversizedSlots) {
    EXPECT_THAT(storage_.Allocate({0, 4}), StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(storage_.Allocate({4, 0}), StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(storage_.Allocate({1U << 30, 1}), StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(storage_.Allocate({1U << 16, 1U << 16}),
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(CpuSlotStorageTest, AllocatedSlotHasTheRequestedSize) {
    absl::StatusOr<std::unique_ptr<Slot>> buffer = storage_.Allocate(kSize);
    ASSERT_THAT(buffer, IsOk());
    EXPECT_EQ((*buffer)->size(), kSize);
}

TEST_F(CpuSlotStorageTest, CopiesRgbxSurfacesExactly) {
    for (uint32_t format : {PIXMAN_x8b8g8r8, PIXMAN_a8b8g8r8}) {
        const TestFrame frame = MakeNoiseFrame(kSize, /*seed=*/4, /*row_padding=*/12);
        const CaptureResult result = CaptureInto(SurfaceOf(frame, format), kSize);
        ASSERT_THAT(result.status, IsOk());
        EXPECT_EQ(result.dimensions, kSize);
        for (ReadFormat out : {ReadFormat::kRgba8888, ReadFormat::kRgb888}) {
            EXPECT_EQ(MaxAbsDiff(ReadSlot(FullRead(kSize), out),
                                 ReferenceRead(frame.pixels(), FullRead(kSize), out)),
                      0);
        }
    }
}

TEST_F(CpuSlotStorageTest, SwizzlesBgrxSurfaces) {
    for (uint32_t format : {PIXMAN_x8r8g8b8, PIXMAN_a8r8g8b8}) {
        const TestFrame frame = MakeNoiseFrame(kSize, /*seed=*/5, /*row_padding=*/4);
        TestFrame bgrx(kSize, /*row_padding=*/4);
        for (uint32_t y = 0; y < kSize.height; ++y) {
            for (uint32_t x = 0; x < kSize.width; ++x) {
                const auto [r, g, b, pad] = frame.Pixel(x, y);
                bgrx.SetPixel(x, y, {b, g, r, pad});
            }
        }
        ASSERT_THAT(CaptureInto(SurfaceOf(bgrx, format), kSize).status, IsOk());
        EXPECT_EQ(MaxAbsDiff(ReadSlot(FullRead(kSize), ReadFormat::kRgba8888),
                             ReferenceRead(frame.pixels(), FullRead(kSize), ReadFormat::kRgba8888)),
                  0);
    }
}

TEST_F(CpuSlotStorageTest, ConvertsOtherFormatsWithPixman) {
    // Red, green, blue, white in RGB565.
    const std::vector<uint16_t> pixels = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    const SurfaceSource src{.data = reinterpret_cast<const uint8_t*>(pixels.data()),
                            .dimensions = {4, 1},
                            .stride = 8,
                            .pixman_format = PIXMAN_r5g6b5};
    ASSERT_THAT(CaptureInto(src, {4, 1}).status, IsOk());
    const ReadOutput out = ReadSlot(FullRead({4, 1}), ReadFormat::kRgb888);
    EXPECT_THAT(out.planes[0], ElementsAre(255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255));
}

// pixman widens an n-bit channel to 8 bits by repeating its top bits.
uint8_t Widen(uint32_t value, int bits) {
    const uint32_t shifted = value << (8 - bits);
    return static_cast<uint8_t>(shifted | (shifted >> bits));
}

// Several rows of distinct pixels, with a padded stride full of garbage: a
// row read at the wrong offset or padding read as pixels changes the output.
TEST_F(CpuSlotStorageTest, PixmanPathHonoursStridesAcrossRows) {
    constexpr Dimensions kRgb565Size{5, 3};
    constexpr size_t kStride = 16;  // 10 bytes of pixels, 6 of padding.
    std::vector<uint32_t> words(kStride / 4 * kRgb565Size.height, 0xA5A5A5A5U);
    auto* bytes = reinterpret_cast<uint8_t*>(words.data());
    std::vector<uint8_t> expected;
    for (uint32_t y = 0; y < kRgb565Size.height; ++y) {
        for (uint32_t x = 0; x < kRgb565Size.width; ++x) {
            const uint32_t r = (x * 7 + y * 11) % 32;
            const uint32_t g = (x * 13 + y * 5 + 3) % 64;
            const uint32_t b = (31 - x * 3 - y * 9) % 32;
            const auto pixel = static_cast<uint16_t>(r << 11 | g << 5 | b);
            std::memcpy(bytes + static_cast<size_t>(y) * kStride + static_cast<size_t>(x) * 2,
                        &pixel, sizeof(pixel));
            expected.insert(expected.end(), {Widen(r, 5), Widen(g, 6), Widen(b, 5)});
        }
    }
    const SurfaceSource src{.data = bytes,
                            .dimensions = kRgb565Size,
                            .stride = kStride,
                            .pixman_format = PIXMAN_r5g6b5};
    ASSERT_THAT(CaptureInto(src, kRgb565Size).status, IsOk());
    EXPECT_THAT(ReadSlot(FullRead(kRgb565Size), ReadFormat::kRgb888).planes[0],
                ElementsAreArray(expected));
}

TEST_F(CpuSlotStorageTest, PixmanRejectsMisalignment) {
    std::vector<uint32_t> words(16);
    auto* bytes = reinterpret_cast<uint8_t*>(words.data());
    const SurfaceSource aligned{
        .data = bytes, .dimensions = {4, 2}, .stride = 8, .pixman_format = PIXMAN_r5g6b5};
    EXPECT_THAT(CaptureInto(aligned, {4, 2}).status, IsOk());

    SurfaceSource odd_data = aligned;
    odd_data.data = bytes + 2;
    EXPECT_THAT(CaptureInto(odd_data, {4, 2}).status, StatusIs(absl::StatusCode::kInvalidArgument));
    SurfaceSource odd_stride = aligned;
    odd_stride.stride = 10;
    EXPECT_THAT(CaptureInto(odd_stride, {4, 2}).status,
                StatusIs(absl::StatusCode::kInvalidArgument));
}

// The copy and swizzle paths take tight rows at an odd address. The buffer
// ends with the last pixel, so under ASAN any read past it fails.
TEST_F(CpuSlotStorageTest, SwizzleTightAndOddPointer) {
    const TestFrame frame = MakeNoiseFrame(kSize, /*seed=*/14);
    for (uint32_t format : {PIXMAN_x8r8g8b8, PIXMAN_x8b8g8r8}) {
        const size_t stride = size_t{kSize.width} * 4;
        std::vector<uint8_t> buffer(1 + stride * kSize.height);
        uint8_t* data = buffer.data() + 1;
        for (uint32_t y = 0; y < kSize.height; ++y) {
            for (uint32_t x = 0; x < kSize.width; ++x) {
                const auto [r, g, b, pad] = frame.Pixel(x, y);
                const std::array<uint8_t, 4> pixel = format == PIXMAN_x8r8g8b8
                                                             ? std::array<uint8_t, 4>{b, g, r, pad}
                                                             : std::array<uint8_t, 4>{r, g, b, pad};
                std::memcpy(data + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4,
                            pixel.data(), pixel.size());
            }
        }
        const SurfaceSource src{
            .data = data, .dimensions = kSize, .stride = stride, .pixman_format = format};
        ASSERT_THAT(CaptureInto(src, kSize).status, IsOk());
        EXPECT_EQ(MaxAbsDiff(ReadSlot(FullRead(kSize), ReadFormat::kRgba8888),
                             ReferenceRead(frame.pixels(), FullRead(kSize), ReadFormat::kRgba8888)),
                  0)
                << "format 0x" << std::hex << format;
    }
}

// Colour comes through unchanged whatever the alpha, and RGBA reads are opaque.
TEST_F(CpuSlotStorageTest, AlphaFormatsViaPixmanReadOpaque) {
    constexpr Dimensions kAlphaSize{3, 2};
    struct Rgba {
        uint32_t r, g, b, a;
    };
    const Rgba pixels[] = {{255, 0, 0, 0},  {0, 255, 0, 0x80}, {0, 0, 255, 0xFF},
                           {12, 34, 56, 0}, {200, 100, 50, 1}, {255, 255, 255, 0x7F}};
    std::vector<uint8_t> expected;
    for (const Rgba& p : pixels)
        expected.insert(expected.end(), {static_cast<uint8_t>(p.r), static_cast<uint8_t>(p.g),
                                         static_cast<uint8_t>(p.b), 0xFF});

    for (uint32_t format : {PIXMAN_b8g8r8a8, PIXMAN_r8g8b8a8}) {
        std::vector<uint32_t> words;
        for (const Rgba& p : pixels) {
            words.push_back(format == PIXMAN_b8g8r8a8 ? p.b << 24 | p.g << 16 | p.r << 8 | p.a
                                                      : p.r << 24 | p.g << 16 | p.b << 8 | p.a);
        }
        const SurfaceSource src{.data = reinterpret_cast<const uint8_t*>(words.data()),
                                .dimensions = kAlphaSize,
                                .stride = size_t{kAlphaSize.width} * 4,
                                .pixman_format = format};
        ASSERT_THAT(CaptureInto(src, kAlphaSize).status, IsOk());
        EXPECT_THAT(ReadSlot(FullRead(kAlphaSize), ReadFormat::kRgba8888).planes[0],
                    ElementsAreArray(expected))
                << "format 0x" << std::hex << format;
    }

    // 5-bit channels; 8-byte rows of 3 pixels and padding.
    std::vector<uint16_t> a1r5g5b5(size_t{4} * kAlphaSize.height, 0xA5A5);
    std::vector<uint8_t> expected_555;
    for (size_t i = 0; i < std::size(pixels); ++i) {
        const Rgba& p = pixels[i];
        const uint32_t r = p.r >> 3, g = p.g >> 3, b = p.b >> 3;
        a1r5g5b5[i / 3 * 4 + i % 3] = static_cast<uint16_t>((p.a & 1) << 15 | r << 10 | g << 5 | b);
        expected_555.insert(expected_555.end(), {Widen(r, 5), Widen(g, 5), Widen(b, 5), 0xFF});
    }
    const SurfaceSource src{.data = reinterpret_cast<const uint8_t*>(a1r5g5b5.data()),
                            .dimensions = kAlphaSize,
                            .stride = 8,
                            .pixman_format = PIXMAN_a1r5g5b5};
    ASSERT_THAT(CaptureInto(src, kAlphaSize).status, IsOk());
    EXPECT_THAT(ReadSlot(FullRead(kAlphaSize), ReadFormat::kRgba8888).planes[0],
                ElementsAreArray(expected_555));
}

// Indexed and gray formats have no palette to read, YUV has planes past the
// surface, and the rest carry no plain RGB. The buffer is large enough for
// every format, so only the format check can reject them.
TEST_F(CpuSlotStorageTest, RejectsIndexedAndYuvFormats) {
    const size_t stride = size_t{kSize.width} * 16;
    std::vector<uint32_t> words(stride / 4 * kSize.height);
    for (uint32_t format : {PIXMAN_c8, PIXMAN_x4c4, PIXMAN_c4, PIXMAN_g8, PIXMAN_x4g4, PIXMAN_g4,
                            PIXMAN_g1, PIXMAN_yuy2, PIXMAN_yv12, PIXMAN_a8, PIXMAN_a1,
                            PIXMAN_a8r8g8b8_sRGB, PIXMAN_rgba_float}) {
        const SurfaceSource src{.data = reinterpret_cast<const uint8_t*>(words.data()),
                                .dimensions = kSize,
                                .stride = stride,
                                .pixman_format = format};
        EXPECT_THAT(CaptureInto(src, kSize).status, StatusIs(absl::StatusCode::kInvalidArgument))
                << "format 0x" << std::hex << format;
    }
}

// A short stride, or one libyuv and pixman can't take as int, is rejected
// by the copy, swizzle and pixman paths alike, before any pixel is read.
TEST_F(CpuSlotStorageTest, ShortOrHugeStrideRejectedOnEveryPath) {
    std::vector<uint32_t> words(size_t{kSize.width} * kSize.height);
    for (const auto [format, bytes_per_pixel] :
         {std::pair{PIXMAN_x8b8g8r8, 4}, std::pair{PIXMAN_x8r8g8b8, 4},
          std::pair{PIXMAN_r5g6b5, 2}}) {
        const size_t row_bytes = size_t{kSize.width} * bytes_per_pixel;
        for (size_t stride : {row_bytes - 1, size_t{std::numeric_limits<int>::max()} + 1}) {
            const SurfaceSource src{.data = reinterpret_cast<const uint8_t*>(words.data()),
                                    .dimensions = kSize,
                                    .stride = stride,
                                    .pixman_format = static_cast<uint32_t>(format)};
            EXPECT_THAT(CaptureInto(src, kSize).status,
                        StatusIs(absl::StatusCode::kInvalidArgument))
                    << "format 0x" << std::hex << format << std::dec << " stride " << stride;
        }
    }
}

TEST_F(CpuSlotStorageTest, RejectsInvalidSources) {
    const TestFrame frame = MakeNoiseFrame(kSize, /*seed=*/6);
    const SurfaceSource good = SurfaceOf(frame, PIXMAN_x8b8g8r8);

    EXPECT_THAT(CaptureInto(good, {kSize.width + 1, kSize.height}).status,
                StatusIs(absl::StatusCode::kInvalidArgument));  // Size mismatch.
    SurfaceSource null_data = good;
    null_data.data = nullptr;
    EXPECT_THAT(CaptureInto(null_data, kSize).status, StatusIs(absl::StatusCode::kInvalidArgument));
    SurfaceSource short_stride = good;
    short_stride.stride = kSize.width * 4 - 1;
    EXPECT_THAT(CaptureInto(short_stride, kSize).status,
                StatusIs(absl::StatusCode::kInvalidArgument));
    SurfaceSource bad_format = good;
    bad_format.pixman_format = 0x12345678;
    EXPECT_THAT(CaptureInto(bad_format, kSize).status,
                StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(CpuSlotStorageTest, ReadRunsTheWholePipeline) {
    const TestFrame frame = MakeNoiseFrame(kSize, /*seed=*/7, /*row_padding=*/8);
    ASSERT_THAT(CaptureInto(SurfaceOf(frame, PIXMAN_x8b8g8r8), kSize).status, IsOk());
    const ReadRequest rotated = FullRead(kSize, ImageRotation::kRotation90);
    EXPECT_EQ(MaxAbsDiff(ReadSlot(rotated, ReadFormat::kRgb888),
                         ReferenceRead(frame.pixels(), rotated, ReadFormat::kRgb888)),
              0);
    const ReadRequest nv12{.crop = Rect{0, 0, kSize.width, kSize.height}, .output = {12, 6}};
    EXPECT_LE(MaxAbsDiff(ReadSlot(nv12, ReadFormat::kNv12),
                         ReferenceRead(frame.pixels(), nv12, ReadFormat::kNv12)),
              kScaledTolerance);
}

// Slots own their memory; the stateless storage may go first.
TEST(CpuSlotStorageLifetimeTest, SlotOutlivesStorage) {
    const TestFrame frame = MakeNoiseFrame(kSize, /*seed=*/15, /*row_padding=*/4);
    auto storage = std::make_unique<CpuSlotStorage>();
    absl::StatusOr<std::unique_ptr<Slot>> slot = storage->Allocate(kSize);
    ASSERT_THAT(slot, IsOk());
    std::optional<CaptureResult> result;
    storage->Capture(**slot, SurfaceOf(frame, PIXMAN_x8b8g8r8),
                     [&](CaptureResult r) { result = std::move(r); });
    ASSERT_TRUE(result.has_value());
    ASSERT_THAT(result->status, IsOk());
    storage.reset();

    Destination dst(kSize, ReadFormat::kRgba8888);
    ASSERT_THAT((*slot)->ReadInto(FullRead(kSize), dst.target()), IsOk());
    EXPECT_EQ(MaxAbsDiff(dst.Pixels(),
                         ReferenceRead(frame.pixels(), FullRead(kSize), ReadFormat::kRgba8888)),
              0);
}

// Step 0 wiring: a LegacySurfaceSource on CpuSlotStorage hands the frame over
// inside Capture.
TEST(CpuSlotStorageSourceTest, SourceHandsTheFrameOverInsideCapture) {
    const TestFrame frame = MakeNoiseFrame(kSize, /*seed=*/8);
    auto source = LegacySurfaceSource::Create(
            std::weak_ptr<IFrameProducer>(),
            LegacySurfaceSource::Hooks{
                .capture =
                        [&frame](LegacySurfaceSource& source) {
                            return source.CaptureFrom(SurfaceOf(frame, PIXMAN_x8b8g8r8));
                        },
            },
            std::make_unique<CpuSlotStorage>());
    std::optional<absl::StatusOr<SlotLease>> latest;
    ASSERT_EQ(source->Capture(CaptureCompletion(
                      [&](absl::StatusOr<SlotLease> captured) { latest = std::move(captured); })),
              IFrameSource::Start::kStarted);
    ASSERT_TRUE(latest.has_value());
    ASSERT_THAT(*latest, IsOk());
    EXPECT_EQ((*latest)->dimensions(), kSize);

    Destination dst(kSize, ReadFormat::kRgba8888);
    ASSERT_THAT((*latest)->ReadInto({}, dst.target()), IsOk());
    EXPECT_EQ(MaxAbsDiff(dst.Pixels(),
                         ReferenceRead(frame.pixels(), FullRead(kSize), ReadFormat::kRgba8888)),
              0);
}

}  // namespace
}  // namespace goldfish::display
