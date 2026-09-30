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

// Shared synthetic test frames and an exact per-pixel floating-point reference
// implementation of the read pipeline (`crop -> rotate -> scale -> convert`).
// Every read implementation (such as `CpuReadPipeline`) is verified
// against `ReferenceRead`: bit-exact for unscaled RGB crop/rotation, within 2
// levels per 8-bit sample for unscaled NV12 output, and within 4 levels for
// bilinearly scaled output.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/types/span.h"

#include "goldfish/display/cpu_read_pipeline.h"
#include "goldfish/display/frame.h"
#include "goldfish/display/geometry.h"

namespace goldfish::display::conformance {

// Maximum per-byte rounding difference between integer fixed-point BT.601
// RGB-to-NV12 conversion and the floating-point reference implementation.
inline constexpr int kNv12Tolerance = 2;
// libyuv's ARGB column filter weighs neighbours by (127 - f) and f with a
// 7-bit fraction, then truncates with >> 7 (scale_common.cc, BLENDER1). That
// darkens bright samples and misplaces sharp edges, so ARGBScale differs from
// the exact bilinear reference by up to 3 on smooth content and up to 4 on
// noise and 1- or 3-pixel checkerboards. These are measured maxima on x86-64,
// for every scaled read including NV12, whose own rounding has not been seen
// to push the total past 4.
inline constexpr int kScaledTolerance = 4;

// An owned frame of R,G,B,X pixels with an optional row padding.
class TestFrame {
  public:
    // Pixels and padding start out zero.
    TestFrame(Dimensions size, size_t row_padding);

    SlotPixels pixels() const;
    Dimensions size() const { return size_; }
    size_t stride() const { return stride_; }
    // All rows, padding included.
    absl::Span<uint8_t> mutable_bytes() { return absl::MakeSpan(bytes_); }

    void SetPixel(uint32_t x, uint32_t y, std::array<uint8_t, 4> rgbx);
    std::array<uint8_t, 4> Pixel(uint32_t x, uint32_t y) const;

  private:
    Dimensions size_;
    size_t stride_;
    std::vector<uint8_t> bytes_;
};

// Every byte, including the undefined fourth one and the row padding, is
// pseudo-random. Any wrong permutation of pixels (a reversed rotation, an
// off-by-one crop) changes the output.
TestFrame MakeNoiseFrame(Dimensions size, uint32_t seed, size_t row_padding = 0);

// Smooth gradients (at most ~16 levels per pixel), for comparing resampled
// output, where fixed-point interpolation differs from exact arithmetic.
TestFrame MakeSmoothFrame(Dimensions size, size_t row_padding = 0);

// Black and white squares of `cell` x `cell` pixels: the largest possible
// step at every cell edge, which is where fixed-point filter weights err most.
TestFrame MakeCheckerboardFrame(Dimensions size, uint32_t cell, size_t row_padding = 0);

// Tightly packed output of one read: one vector per plane (NV12: Y, then UV).
struct ReadOutput {
    std::vector<std::vector<uint8_t>> planes;
    Dimensions size{0, 0};
    ReadFormat format = ReadFormat::kRgba8888;
};

// The normative pipeline, one pixel at a time: crop, rotate counter-clockwise,
// bilinear scale with aligned pixel centres and edge clamping, convert
// (BT.601 limited range for NV12, U/V from the 2x2 average).
ReadOutput ReferenceRead(const SlotPixels& src, const ReadRequest& read, ReadFormat format);

// A destination for a read, with `row_padding` extra bytes after every row
// of every plane, all set to `sentinel`. Without padding, planes pass stride 0
// (tightly packed).
class Destination {
  public:
    Destination(Dimensions output, ReadFormat format, size_t row_padding = 0,
                uint8_t sentinel = kSentinel);

    const ReadTarget& target() const { return target_; }

    // The written pixels, without padding.
    ReadOutput Pixels() const;

    // True if every padding byte still holds the sentinel.
    bool PaddingIntact() const;

    static constexpr uint8_t kSentinel = 0xAB;

  private:
    struct PlaneMemory {
        std::vector<uint8_t> bytes;
        size_t row_bytes = 0;
        size_t stride = 0;
        size_t rows = 0;
    };

    Dimensions output_;
    ReadFormat format_;
    uint8_t sentinel_;
    std::vector<PlaneMemory> planes_;
    ReadTarget target_;
};

// Largest absolute difference between two outputs, sample by sample.
// @return 256 if the outputs differ in size, format or plane count.
int MaxAbsDiff(const ReadOutput& a, const ReadOutput& b);

// Where the largest difference is, for failure messages.
std::string DescribeWorstDifference(const ReadOutput& actual, const ReadOutput& expected);

}  // namespace goldfish::display::conformance
