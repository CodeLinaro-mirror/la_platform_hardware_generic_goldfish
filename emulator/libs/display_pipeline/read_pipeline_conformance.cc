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
#include "goldfish/display/test/read_pipeline_conformance.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include "absl/strings/str_format.h"

namespace goldfish::display::conformance {
namespace {

using Rgbx = std::array<uint8_t, 4>;
using RgbF = std::array<double, 3>;

uint32_t Hash(uint32_t x, uint32_t y, uint32_t seed, uint32_t channel) {
    uint64_t h = (uint64_t{x} << 40) ^ (uint64_t{y} << 16) ^ (uint64_t{seed} << 4) ^ channel;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return static_cast<uint32_t>(h);
}

uint8_t ToByte(double value) {
    return static_cast<uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

// A tight, working image of R,G,B,X pixels.
struct Image {
    Dimensions size{0, 0};
    std::vector<Rgbx> pixels;

    const Rgbx& At(uint32_t x, uint32_t y) const { return pixels[size_t{y} * size.width + x]; }
};

Image Crop(const SlotPixels& src, const Rect& crop) {
    Image out{.size = {crop.width, crop.height}};
    out.pixels.reserve(size_t{crop.width} * crop.height);
    for (uint32_t y = 0; y < crop.height; ++y) {
        const uint8_t* row = src.data + (size_t{crop.y} + y) * src.stride;
        for (uint32_t x = 0; x < crop.width; ++x) {
            const uint8_t* p = row + (size_t{crop.x} + x) * 4;
            out.pixels.push_back({p[0], p[1], p[2], p[3]});
        }
    }
    return out;
}

// Counter-clockwise: at 90 degrees the top-right pixel becomes the top-left.
Image Rotate(const Image& in, ImageRotation rotation) {
    const uint32_t w = in.size.width;
    const uint32_t h = in.size.height;
    const bool quarter =
            rotation == ImageRotation::kRotation90 || rotation == ImageRotation::kRotation270;
    Image out{.size = quarter ? Dimensions{h, w} : Dimensions{w, h}};
    out.pixels.reserve(in.pixels.size());
    for (uint32_t y = 0; y < out.size.height; ++y) {
        for (uint32_t x = 0; x < out.size.width; ++x) {
            switch (rotation) {
            case ImageRotation::kRotation0:
                out.pixels.push_back(in.At(x, y));
                break;
            case ImageRotation::kRotation90:
                out.pixels.push_back(in.At(w - 1 - y, x));
                break;
            case ImageRotation::kRotation180:
                out.pixels.push_back(in.At(w - 1 - x, h - 1 - y));
                break;
            case ImageRotation::kRotation270:
                out.pixels.push_back(in.At(y, h - 1 - x));
                break;
            }
        }
    }
    return out;
}

// Output coordinate o + 0.5 samples input coordinate (o + 0.5) * in / out,
// clamped to the edge pixels.
struct Tap {
    uint32_t lo;
    uint32_t hi;
    double fraction;
};

Tap TapFor(uint32_t o, uint32_t in, uint32_t out) {
    const double s = std::clamp((o + 0.5) * in / out - 0.5, 0.0, static_cast<double>(in - 1));
    const auto lo = static_cast<uint32_t>(std::floor(s));
    return Tap{lo, std::min(lo + 1, in - 1), s - lo};
}

Image Scale(const Image& in, Dimensions output) {
    if (in.size == output) return in;
    Image out{.size = output};
    out.pixels.reserve(size_t{output.width} * output.height);
    for (uint32_t oy = 0; oy < output.height; ++oy) {
        const Tap ty = TapFor(oy, in.size.height, output.height);
        for (uint32_t ox = 0; ox < output.width; ++ox) {
            const Tap tx = TapFor(ox, in.size.width, output.width);
            Rgbx pixel{};
            for (size_t c = 0; c < 4; ++c) {
                const double top = in.At(tx.lo, ty.lo)[c] * (1 - tx.fraction) +
                                   in.At(tx.hi, ty.lo)[c] * tx.fraction;
                const double bottom = in.At(tx.lo, ty.hi)[c] * (1 - tx.fraction) +
                                      in.At(tx.hi, ty.hi)[c] * tx.fraction;
                pixel[c] = ToByte(top * (1 - ty.fraction) + bottom * ty.fraction);
            }
            out.pixels.push_back(pixel);
        }
    }
    return out;
}

uint8_t Bt601Y(const RgbF& rgb) {
    return ToByte(16 + (65.481 * rgb[0] + 128.553 * rgb[1] + 24.966 * rgb[2]) / 255);
}
uint8_t Bt601U(const RgbF& rgb) {
    return ToByte(128 + (-37.797 * rgb[0] - 74.203 * rgb[1] + 112.0 * rgb[2]) / 255);
}
uint8_t Bt601V(const RgbF& rgb) {
    return ToByte(128 + (112.0 * rgb[0] - 93.786 * rgb[1] - 18.214 * rgb[2]) / 255);
}

ReadOutput Convert(const Image& image, ReadFormat format) {
    ReadOutput out{.size = image.size, .format = format};
    const uint32_t w = image.size.width;
    const uint32_t h = image.size.height;
    switch (format) {
    case ReadFormat::kRgba8888:
    case ReadFormat::kRgb888: {
        const bool rgba = format == ReadFormat::kRgba8888;
        std::vector<uint8_t>& plane = out.planes.emplace_back();
        for (const Rgbx& p : image.pixels) {
            plane.insert(plane.end(), {p[0], p[1], p[2]});
            if (rgba) plane.push_back(0xFF);
        }
        break;
    }
    case ReadFormat::kNv12: {
        std::vector<uint8_t>& y_plane = out.planes.emplace_back();
        for (const Rgbx& p : image.pixels) {
            y_plane.push_back(Bt601Y({static_cast<double>(p[0]), static_cast<double>(p[1]),
                                      static_cast<double>(p[2])}));
        }
        std::vector<uint8_t>& uv_plane = out.planes.emplace_back();
        for (uint32_t y = 0; y + 1 < h; y += 2) {
            for (uint32_t x = 0; x + 1 < w; x += 2) {
                RgbF average{};
                for (const Rgbx* p : {&image.At(x, y), &image.At(x + 1, y), &image.At(x, y + 1),
                                      &image.At(x + 1, y + 1)}) {
                    for (size_t c = 0; c < 3; ++c) average[c] += (*p)[c] / 4.0;
                }
                uv_plane.push_back(Bt601U(average));
                uv_plane.push_back(Bt601V(average));
            }
        }
        break;
    }
    }
    return out;
}

}  // namespace

TestFrame::TestFrame(Dimensions size, size_t row_padding)
        : size_(size)
        , stride_(size_t{size.width} * 4 + row_padding)
        , bytes_(stride_ * size.height) {}

SlotPixels TestFrame::pixels() const {
    return SlotPixels{.data = bytes_.data(), .stride = stride_, .dimensions = size_};
}

void TestFrame::SetPixel(uint32_t x, uint32_t y, std::array<uint8_t, 4> rgbx) {
    std::copy(rgbx.begin(), rgbx.end(), bytes_.begin() + y * stride_ + size_t{x} * 4);
}

std::array<uint8_t, 4> TestFrame::Pixel(uint32_t x, uint32_t y) const {
    const uint8_t* p = bytes_.data() + y * stride_ + size_t{x} * 4;
    return {p[0], p[1], p[2], p[3]};
}

TestFrame MakeNoiseFrame(Dimensions size, uint32_t seed, size_t row_padding) {
    TestFrame frame(size, row_padding);
    const size_t stride = frame.stride();
    absl::Span<uint8_t> bytes = frame.mutable_bytes();
    for (size_t i = 0; i < bytes.size(); ++i) {
        const size_t column = i % stride;
        bytes[i] = static_cast<uint8_t>(Hash(static_cast<uint32_t>(column / 4),
                                             static_cast<uint32_t>(i / stride), seed,
                                             static_cast<uint32_t>(column % 4)));
    }
    return frame;
}

TestFrame MakeSmoothFrame(Dimensions size, size_t row_padding) {
    TestFrame frame(size, row_padding);
    for (uint32_t y = 0; y < size.height; ++y) {
        for (uint32_t x = 0; x < size.width; ++x) {
            frame.SetPixel(x, y,
                           {ToByte(128 + 100 * std::sin(x * 0.15 + y * 0.05)),
                            ToByte(128 + 100 * std::cos(y * 0.12 - x * 0.03)),
                            ToByte(255.0 * (x + y) / (size.width + size.height)),
                            static_cast<uint8_t>(x * 31 + y * 17)});
        }
    }
    return frame;
}

TestFrame MakeCheckerboardFrame(Dimensions size, uint32_t cell, size_t row_padding) {
    TestFrame frame(size, row_padding);
    const uint32_t side = std::max(cell, uint32_t{1});
    for (uint32_t y = 0; y < size.height; ++y) {
        for (uint32_t x = 0; x < size.width; ++x) {
            const uint8_t value = ((x / side) + (y / side)) % 2 == 0 ? 0xFF : 0x00;
            frame.SetPixel(x, y, {value, value, value, static_cast<uint8_t>(~value)});
        }
    }
    return frame;
}

ReadOutput ReferenceRead(const SlotPixels& src, const ReadRequest& read, ReadFormat format) {
    const Rect crop = read.crop.value_or(Rect{0, 0, src.dimensions.width, src.dimensions.height});
    return Convert(Scale(Rotate(Crop(src, crop), read.rotation), read.output), format);
}

Destination::Destination(Dimensions output, ReadFormat format, size_t row_padding, uint8_t sentinel)
        : output_(output), format_(format), sentinel_(sentinel) {
    auto add_plane = [&](size_t row_bytes, size_t rows) -> Plane {
        PlaneMemory& memory = planes_.emplace_back();
        memory.row_bytes = row_bytes;
        memory.stride = row_bytes + row_padding;
        memory.rows = rows;
        memory.bytes.assign(memory.stride * rows, sentinel);
        // Without padding, exercise the "0 means tight" convention.
        return Plane{absl::MakeSpan(memory.bytes), row_padding == 0 ? 0 : memory.stride};
    };
    planes_.reserve(2);
    switch (format) {
    case ReadFormat::kRgba8888:
        target_ = Rgba8888Target{add_plane(size_t{output.width} * 4, output.height)};
        break;
    case ReadFormat::kRgb888:
        target_ = Rgb888Target{add_plane(size_t{output.width} * 3, output.height)};
        break;
    case ReadFormat::kNv12: {
        Plane y = add_plane(output.width, output.height);
        Plane uv = add_plane(output.width, output.height / 2);
        target_ = Nv12Target{y, uv};
        break;
    }
    }
}

ReadOutput Destination::Pixels() const {
    ReadOutput out{.size = output_, .format = format_};
    for (const PlaneMemory& memory : planes_) {
        std::vector<uint8_t>& plane = out.planes.emplace_back();
        for (size_t row = 0; row < memory.rows; ++row) {
            const auto begin = memory.bytes.begin() + row * memory.stride;
            plane.insert(plane.end(), begin, begin + memory.row_bytes);
        }
    }
    return out;
}

bool Destination::PaddingIntact() const {
    for (const PlaneMemory& memory : planes_) {
        for (size_t row = 0; row < memory.rows; ++row) {
            const auto begin = memory.bytes.begin() + row * memory.stride;
            if (!std::all_of(begin + memory.row_bytes, begin + memory.stride,
                             [&](uint8_t b) { return b == sentinel_; })) {
                return false;
            }
        }
    }
    return true;
}

int MaxAbsDiff(const ReadOutput& a, const ReadOutput& b) {
    if (a.size != b.size || a.format != b.format || a.planes.size() != b.planes.size()) {
        return 256;
    }
    int worst = 0;
    for (size_t p = 0; p < a.planes.size(); ++p) {
        if (a.planes[p].size() != b.planes[p].size()) return 256;
        for (size_t i = 0; i < a.planes[p].size(); ++i) {
            worst = std::max(worst, std::abs(int{a.planes[p][i]} - int{b.planes[p][i]}));
        }
    }
    return worst;
}

std::string DescribeWorstDifference(const ReadOutput& actual, const ReadOutput& expected) {
    if (MaxAbsDiff(actual, expected) == 256) return "outputs differ in shape";
    int worst = -1;
    std::string where;
    for (size_t p = 0; p < actual.planes.size(); ++p) {
        for (size_t i = 0; i < actual.planes[p].size(); ++i) {
            const int diff = std::abs(int{actual.planes[p][i]} - int{expected.planes[p][i]});
            if (diff > worst) {
                worst = diff;
                where = absl::StrFormat("plane %d byte %d: actual %d, expected %d", p, i,
                                        actual.planes[p][i], expected.planes[p][i]);
            }
        }
    }
    return where;
}

}  // namespace goldfish::display::conformance
