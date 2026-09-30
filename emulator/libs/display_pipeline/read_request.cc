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
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>
#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"

#include "android/status/status_macros.h"
#include "goldfish/display/frame.h"

namespace goldfish::display {
namespace {

static_assert(sizeof(size_t) >= sizeof(uint64_t), "Layout math assumes a 64-bit size_t");

// libyuv takes widths, heights and strides as int.
constexpr uint64_t kMaxLibyuvInt = std::numeric_limits<int>::max();
constexpr uint8_t kBlackY = 16;
constexpr uint8_t kNeutralChroma = 128;

/**
 * Fills the active `elements_per_row` of each of `rows` rows in `plane` with
 * `value`, coalescing into a single fill when rows are tightly packed and
 * leaving any row padding untouched.
 */
template <typename Element>
void FillRows(const Plane& plane, size_t elements_per_row, size_t rows, Element value) {
    static_assert(alignof(Element) == 1, "Plane rows may have arbitrary byte alignment");
    const size_t row_bytes = elements_per_row * sizeof(Element);
    if (rows == 0 || row_bytes == 0) return;

    const size_t stride = (plane.stride == 0) ? row_bytes : plane.stride;
    uint8_t* dst = plane.data.data();

    if (stride == row_bytes) {
        std::fill_n(reinterpret_cast<Element*>(dst), elements_per_row * rows, value);
    } else {
        for (size_t y = 0; y < rows; ++y) {
            std::fill_n(reinterpret_cast<Element*>(dst), elements_per_row, value);
            dst += stride;
        }
    }
}

/** Returns `{width, height}` after applying `rotation` (swapping axes for 90° and 270°). */
Dimensions Rotated(uint32_t width, uint32_t height, ImageRotation rotation) {
    if (rotation == ImageRotation::kRotation90 || rotation == ImageRotation::kRotation270) {
        return {height, width};
    }
    return {width, height};
}

/** Rounds `value` down to the nearest even integer. */
uint32_t RoundDownToEven(uint32_t value) {
    return value & ~uint32_t{1};
}

/** Returns the byte width of one sample in plane 0 for `format` (`4` for RGBA, `3` for RGB, `1` for
 * NV12 Y). */
uint64_t BytesPerPixel(ReadFormat format) {
    switch (format) {
    case ReadFormat::kRgba8888:
        return 4;
    case ReadFormat::kRgb888:
        return 3;
    case ReadFormat::kNv12:
        return 1;
    }
    return 4;
}

/** Returns the number of destination planes for `format`. */
int PlaneCount(ReadFormat format) {
    return format == ReadFormat::kNv12 ? 2 : 1;
}

/** Extracts the `ReadFormat` corresponding to the active alternative of `target`. */
ReadFormat FormatOf(const ReadTarget& target) {
    switch (target.index()) {
    case 1:
        return ReadFormat::kRgb888;
    case 2:
        return ReadFormat::kNv12;
    default:
        return ReadFormat::kRgba8888;
    }
}

/** Validates that `frame` is non-empty and fits within `libyuv`'s signed `int` coordinate limits.
 */
absl::Status CheckFrame(Dimensions frame) {
    if (frame.width == 0 || frame.height == 0 || frame.width > kMaxLibyuvInt ||
        frame.height > kMaxLibyuvInt) {
        return absl::InvalidArgumentError(
                absl::StrFormat("Cannot read a %dx%d frame", frame.width, frame.height));
    }
    return absl::OkStatus();
}

/** Validates that `crop` is non-empty and lies entirely within `frame` without 32-bit overflow. */
absl::Status CheckCrop(Dimensions frame, const Rect& crop) {
    if (crop.width == 0 || crop.height == 0) {
        return absl::InvalidArgumentError(
                absl::StrFormat("Crop %dx%d is empty", crop.width, crop.height));
    }
    if (uint64_t{crop.x} + crop.width > frame.width ||
        uint64_t{crop.y} + crop.height > frame.height) {
        return absl::InvalidArgumentError(
                absl::StrFormat("Crop %dx%d at (%d, %d) is outside the %dx%d frame", crop.width,
                                crop.height, crop.x, crop.y, frame.width, frame.height));
    }
    return absl::OkStatus();
}

/** Validates that `crop` is at least `2x2` so 4:2:0 `NV12` even-dimension rounding produces a
 * non-empty image. */
absl::Status CheckNv12Crop(const Rect& crop) {
    if (crop.width < 2 || crop.height < 2) {
        return absl::InvalidArgumentError(absl::StrFormat(
                "NV12 needs a crop of at least 2x2, got %dx%d", crop.width, crop.height));
    }
    return absl::OkStatus();
}

/** Validates that `rotation` is one of the four orthogonal `ImageRotation` values. */
absl::Status CheckRotation(ImageRotation rotation) {
    switch (rotation) {
    case ImageRotation::kRotation0:
    case ImageRotation::kRotation90:
    case ImageRotation::kRotation180:
    case ImageRotation::kRotation270:
        return absl::OkStatus();
    }
    return absl::InvalidArgumentError(absl::StrFormat("Rotation %d is not a multiple of 90 degrees",
                                                      static_cast<int>(rotation)));
}

/** Validates that explicit `output` dimensions are non-zero and do not upscale beyond
 * `rotated_crop`. */
absl::Status CheckOutputFits(Dimensions output, Dimensions rotated_crop) {
    if (output.width == 0 || output.height == 0) {
        return absl::InvalidArgumentError(
                absl::StrFormat("Output %dx%d has a zero dimension", output.width, output.height));
    }
    if (output.width > rotated_crop.width || output.height > rotated_crop.height) {
        return absl::InvalidArgumentError(absl::StrFormat(
                "Output %dx%d is larger than the rotated crop %dx%d (no upscaling)", output.width,
                output.height, rotated_crop.width, rotated_crop.height));
    }
    return absl::OkStatus();
}

/** Validates that one output row of `format` fits libyuv's `int` strides. */
absl::Status CheckOutputRowBytes(Dimensions output, ReadFormat format) {
    // Every plane's row is `width * BytesPerPixel` bytes (NV12 UV is `width`).
    const uint64_t row_bytes = uint64_t{output.width} * BytesPerPixel(format);
    if (row_bytes > kMaxLibyuvInt) {
        return absl::InvalidArgumentError(absl::StrFormat(
                "Output rows of %d bytes are too large for %d pixels", row_bytes, output.width));
    }
    return absl::OkStatus();
}

/**
 * Validates `request` against `frame` and resolves default `crop`/`output`
 * dimensions and `kNv12` even-dimension rounding without checking destination planes.
 */
absl::StatusOr<ReadRequest> ResolveGeometry(Dimensions frame, const ReadRequest& request,
                                            ReadFormat format) {
    RETURN_IF_ERROR(CheckFrame(frame));
    RETURN_IF_ERROR(CheckRotation(request.rotation));
    Rect crop = request.crop.value_or(Rect{0, 0, frame.width, frame.height});
    RETURN_IF_ERROR(CheckCrop(frame, crop));

    const bool nv12 = format == ReadFormat::kNv12;
    if (nv12) {
        RETURN_IF_ERROR(CheckNv12Crop(crop));
    }

    Dimensions output = request.output;
    if (output == Dimensions{0, 0}) {
        if (nv12) {
            crop.width = RoundDownToEven(crop.width);
            crop.height = RoundDownToEven(crop.height);
        }
        output = Rotated(crop.width, crop.height, request.rotation);
    } else {
        const Dimensions rotated_crop = Rotated(crop.width, crop.height, request.rotation);
        RETURN_IF_ERROR(CheckOutputFits(output, rotated_crop));
        if (nv12) {
            // The crop is at least 2x2, so this never exceeds the rotated crop.
            output.width = std::max(uint32_t{2}, RoundDownToEven(output.width));
            output.height = std::max(uint32_t{2}, RoundDownToEven(output.height));
        }
    }
    RETURN_IF_ERROR(CheckOutputRowBytes(output, format));
    return ReadRequest{.crop = crop,
                       .rotation = request.rotation,
                       .output = output,
                       .yuv_matrix = request.yuv_matrix};
}

/** Validates that `plane` has a valid row stride (`[row_bytes, kMaxLibyuvInt]`) and enough capacity
 * for `rows` rows. */
absl::Status CheckPlane(const Plane& plane, uint64_t row_bytes, uint64_t rows,
                        std::string_view name) {
    const uint64_t stride = plane.stride == 0 ? row_bytes : plane.stride;
    if (stride < row_bytes) {
        return absl::InvalidArgumentError(absl::StrFormat(
                "%s stride %d is shorter than one %d-byte row", name, stride, row_bytes));
    }
    if (stride > kMaxLibyuvInt) {
        return absl::InvalidArgumentError(
                absl::StrFormat("%s stride %d is too large", name, stride));
    }
    if (rows == 0) {
        return absl::InvalidArgumentError(absl::StrFormat("%s plane has no rows", name));
    }
    // stride * rows <= size, without overflow.
    if (stride > plane.data.size() / rows) {
        return absl::InvalidArgumentError(
                absl::StrFormat("%s plane holds %d bytes, needs %d rows of %d", name,
                                plane.data.size(), rows, stride));
    }
    return absl::OkStatus();
}

/** Validates all destination planes in `target` against the resolved `output` dimensions. */
absl::Status CheckTargetPlanes(Dimensions output, const ReadTarget& target) {
    const uint64_t width = output.width;
    const uint64_t height = output.height;
    if (const auto* rgba = std::get_if<Rgba8888Target>(&target)) {
        return CheckPlane(rgba->plane, width * 4, height, "RGBA");
    }
    if (const auto* rgb = std::get_if<Rgb888Target>(&target)) {
        return CheckPlane(rgb->plane, width * 3, height, "RGB888");
    }
    const auto& nv12 = std::get<Nv12Target>(target);
    RETURN_IF_ERROR(CheckPlane(nv12.y, width, height, "NV12 Y"));
    return CheckPlane(nv12.uv, width, height / 2, "NV12 UV");
}

/** Resolves `request` geometry against `frame` and validates that `target`'s planes fit the
 * resolved output. */
absl::StatusOr<ReadRequest> ResolveRead(Dimensions frame, const ReadRequest& request,
                                        const ReadTarget& target) {
    absl::StatusOr<ReadRequest> read = ResolveGeometry(frame, request, FormatOf(target));
    if (!read.ok()) return read;
    RETURN_IF_ERROR(CheckTargetPlanes(read->output, target));
    return read;
}

/** Fills `dst` with opaque black (`R=0, G=0, B=0, A=255` for RGB/RGBA; `Y=16, U=128, V=128` for
 * BT.601 NV12). */
absl::Status FillBlank(const ReadRequest& read, const ReadTarget& dst) {
    const size_t width = read.output.width;
    const size_t height = read.output.height;
    if (const auto* rgba = std::get_if<Rgba8888Target>(&dst)) {
        constexpr std::array<uint8_t, 4> kOpaqueBlack = {0, 0, 0, 0xFF};
        FillRows(rgba->plane, width, height, kOpaqueBlack);
    } else if (const auto* rgb = std::get_if<Rgb888Target>(&dst)) {
        FillRows(rgb->plane, width * 3, height, uint8_t{0});
    } else {
        const auto& nv12 = std::get<Nv12Target>(dst);
        FillRows(nv12.y, width, height, kBlackY);
        FillRows(nv12.uv, width, height / 2, kNeutralChroma);
    }
    return absl::OkStatus();
}

/** Bufferless `Slot` implementation returned by `Slot::Blank()` that fills every read with opaque
 * black. */
class BlankSlot final : public Slot {
  public:
    explicit BlankSlot(Dimensions size) : size_(size) {}
    Dimensions size() const override { return size_; }

  protected:
    absl::Status DoReadInto(const ReadRequest& read, const ReadTarget& dst) const override {
        return FillBlank(read, dst);
    }

  private:
    const Dimensions size_;
};

/** Computes the tightly packed `ReadLayout` (plane offsets, strides, and `total_bytes`) for a
 * resolved `read`. */
ReadLayout MakeLayout(const ReadRequest& read, ReadFormat format) {
    ReadLayout layout{
        .dimensions = read.output, .plane_count = PlaneCount(format), .format = format};
    const uint64_t width = read.output.width;
    const uint64_t height = read.output.height;
    const uint64_t stride = width * BytesPerPixel(format);
    layout.planes[0] = PlaneLayout{.offset = 0, .stride = stride, .bytes = stride * height};
    layout.total_bytes = layout.planes[0].bytes;
    if (format == ReadFormat::kNv12) {
        layout.planes[1] = PlaneLayout{
            .offset = layout.total_bytes, .stride = width, .bytes = width * (height / 2)};
        layout.total_bytes += layout.planes[1].bytes;
    }
    return layout;
}

}  // namespace

ReadTarget ReadLayout::TargetFor(uint8_t* data) const {
    const auto make_plane = [data, total = total_bytes](const PlaneLayout& pl) {
        if (data == nullptr || total == 0) {
            return Plane{};
        }
        return Plane{.data = absl::MakeSpan(data + pl.offset, pl.bytes), .stride = pl.stride};
    };
    switch (format) {
    case ReadFormat::kRgba8888:
        return Rgba8888Target{.plane = make_plane(planes[0])};
    case ReadFormat::kRgb888:
        return Rgb888Target{.plane = make_plane(planes[0])};
    case ReadFormat::kNv12:
        return Nv12Target{.y = make_plane(planes[0]), .uv = make_plane(planes[1])};
    }
    return Rgba8888Target{};
}

ReadLayout Slot::LayoutFor(Dimensions frame, const ReadRequest& request, ReadFormat format) {
    absl::StatusOr<ReadRequest> read = ResolveGeometry(frame, request, format);
    if (!read.ok()) return ReadLayout{.plane_count = PlaneCount(format), .format = format};
    return MakeLayout(*read, format);
}

absl::Status Slot::ReadInto(const ReadRequest& request, const ReadTarget& dst) const {
    ASSIGN_OR_RETURN(const ReadRequest read, ResolveRead(size(), request, dst));
    return DoReadInto(read, dst);
}

absl::StatusOr<Dimensions> Slot::ReadWithTargetAllocator(
        const ReadRequest& request, ReadFormat format,
        absl::FunctionRef<absl::StatusOr<ReadTarget>(const ReadLayout& layout)> allocator) const {
    ASSIGN_OR_RETURN(const ReadRequest read, ResolveGeometry(size(), request, format));
    const ReadLayout layout = MakeLayout(read, format);
    ASSIGN_OR_RETURN(const ReadTarget dst, allocator(layout));
    if (FormatOf(dst) != format) {
        return absl::InvalidArgumentError("Allocator returned target of mismatched format");
    }
    RETURN_IF_ERROR(CheckTargetPlanes(read.output, dst));
    RETURN_IF_ERROR(DoReadInto(read, dst));
    return read.output;
}

absl::StatusOr<Dimensions> Slot::ReadInto(
        const ReadRequest& request, ReadFormat format,
        absl::FunctionRef<absl::StatusOr<uint8_t*>(const ReadLayout& layout)> allocator) const {
    return ReadWithTargetAllocator(
            request, format, [&](const ReadLayout& layout) -> absl::StatusOr<ReadTarget> {
                ASSIGN_OR_RETURN(uint8_t* ptr, allocator(layout));
                if (ptr == nullptr) {
                    return absl::InvalidArgumentError("Allocator returned null buffer");
                }
                return layout.TargetFor(ptr);
            });
}

std::unique_ptr<const Slot> Slot::Blank(Dimensions size) {
    return std::make_unique<const BlankSlot>(size);
}

}  // namespace goldfish::display
