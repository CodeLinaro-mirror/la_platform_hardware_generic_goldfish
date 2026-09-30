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

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#include "absl/strings/str_format.h"

#include "libyuv/convert_from_argb.h"

extern "C" {
#include "pixman.h"
}

#include "goldfish/display/cpu_read_pipeline.h"
#include "goldfish/perfetto/perfetto_categories.h"

namespace goldfish::display {
namespace {

// PIXMAN_x8b8g8r8 is R,G,B,X in memory only on little-endian hosts.
static_assert(std::endian::native == std::endian::little);

constexpr size_t kBytesPerPixel = 4;
constexpr std::align_val_t kRowAlignment{64};  // libyuv's widest SIMD loads.
// pixman and libyuv take strides and sizes as int.
constexpr uint64_t kMaxBytes = std::numeric_limits<int>::max();

class CpuSlotBuffer : public Slot {
  public:
    // @return Null if out of memory.
    static std::unique_ptr<CpuSlotBuffer> Create(Dimensions size, size_t stride) {
        void* data = ::operator new(stride * size.height, kRowAlignment, std::nothrow);
        if (data == nullptr) return nullptr;
        return std::make_unique<CpuSlotBuffer>(size, stride, static_cast<uint8_t*>(data));
    }

    CpuSlotBuffer(Dimensions size, size_t stride, uint8_t* data)
            : size_(size), stride_(stride), data_(data) {}
    ~CpuSlotBuffer() override { ::operator delete(data_, kRowAlignment); }

    CpuSlotBuffer(const CpuSlotBuffer&) = delete;
    CpuSlotBuffer& operator=(const CpuSlotBuffer&) = delete;

    Dimensions size() const override { return size_; }
    size_t Stride() const { return stride_; }
    uint8_t* Data() { return data_; }
    SlotPixels Pixels() const { return {.data = data_, .stride = stride_, .dimensions = size_}; }

  protected:
    // CpuReadPipeline on the slot's memory.
    absl::Status DoReadInto(const ReadRequest& read, const ReadTarget& dst) const override {
        TRACE_EVENT("display", "CpuSlotBuffer::ReadInto", "w", read.output.width, "h",
                    read.output.height);
        return CpuReadPipeline(Pixels(), read, dst);
    }

  private:
    const Dimensions size_;
    const size_t stride_;
    uint8_t* const data_;
};

struct PixmanImageDeleter {
    void operator()(pixman_image_t* image) const { pixman_image_unref(image); }
};
using PixmanImage = std::unique_ptr<pixman_image_t, PixmanImageDeleter>;

absl::Status CheckStride(const SurfaceSource& src, uint64_t row_bytes) {
    if (src.stride < row_bytes || src.stride > kMaxBytes) {
        return absl::InvalidArgumentError(absl::StrFormat(
                "Surface stride %d does not fit %d-byte rows", src.stride, row_bytes));
    }
    return absl::OkStatus();
}

void CopyRows(const SurfaceSource& src, CpuSlotBuffer& dst) {
    const size_t row_bytes = size_t{src.dimensions.width} * kBytesPerPixel;
    for (uint32_t y = 0; y < src.dimensions.height; ++y) {
        std::memcpy(dst.Data() + y * dst.Stride(), src.data + y * src.stride, row_bytes);
    }
}

// Formats pixman can read from one plane of `stride * height` bytes with no
// palette. pixman_format_supported_source() also accepts indexed and gray
// formats, whose fetchers dereference the image's null palette, and YUV, whose
// chroma planes lie past the surface. sRGB and float types are excluded too,
// as they are not converted to plain 8-bit channels. Alpha-only formats carry
// no colour to display.
bool IsPlainRgbFormat(pixman_format_code_t format) {
    switch (PIXMAN_FORMAT_TYPE(format)) {
    case PIXMAN_TYPE_ARGB:
    case PIXMAN_TYPE_ABGR:
    case PIXMAN_TYPE_BGRA:
    case PIXMAN_TYPE_RGBA:
        return pixman_format_supported_source(format);
    default:
        return false;
    }
}

absl::Status ConvertWithPixman(const SurfaceSource& src, CpuSlotBuffer& dst) {
    const auto format = static_cast<pixman_format_code_t>(src.pixman_format);
    if (!IsPlainRgbFormat(format)) {
        return absl::InvalidArgumentError(
                absl::StrFormat("Unsupported surface format 0x%x", src.pixman_format));
    }
    const uint64_t row_bytes = (uint64_t{src.dimensions.width} * PIXMAN_FORMAT_BPP(format) + 7) / 8;
    if (absl::Status status = CheckStride(src, row_bytes); !status.ok()) return status;
    // pixman reads the surface as 32-bit words.
    if (src.stride % 4 != 0 || reinterpret_cast<uintptr_t>(src.data) % 4 != 0) {
        return absl::InvalidArgumentError("Surface rows are not 4-byte aligned");
    }
    const int width = static_cast<int>(src.dimensions.width);
    const int height = static_cast<int>(src.dimensions.height);
    // pixman never writes to a composite source.
    PixmanImage in(pixman_image_create_bits_no_clear(
            format, width, height, reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(src.data)),
            static_cast<int>(src.stride)));
    PixmanImage out(pixman_image_create_bits_no_clear(PIXMAN_x8b8g8r8, width, height,
                                                      reinterpret_cast<uint32_t*>(dst.Data()),
                                                      static_cast<int>(dst.Stride())));
    if (in == nullptr || out == nullptr) {
        return absl::InternalError("pixman_image_create_bits failed");
    }
    pixman_image_composite32(PIXMAN_OP_SRC, in.get(), nullptr, out.get(), 0, 0, 0, 0, 0, 0, width,
                             height);
    return absl::OkStatus();
}

absl::Status CopySurface(const SurfaceSource& src, CpuSlotBuffer& dst) {
    if (src.data == nullptr) return absl::InvalidArgumentError("Surface has no pixels");
    if (src.dimensions != dst.size()) {
        return absl::InvalidArgumentError(
                absl::StrFormat("Surface is %dx%d, slot is %dx%d", src.dimensions.width,
                                src.dimensions.height, dst.size().width, dst.size().height));
    }
    const uint64_t row_bytes = uint64_t{src.dimensions.width} * kBytesPerPixel;
    switch (src.pixman_format) {
    case PIXMAN_x8b8g8r8:
    case PIXMAN_a8b8g8r8:  // Already R,G,B,X.
        if (absl::Status status = CheckStride(src, row_bytes); !status.ok()) return status;
        CopyRows(src, dst);
        return absl::OkStatus();
    case PIXMAN_x8r8g8b8:
    case PIXMAN_a8r8g8b8:  // B,G,R,X: libyuv "ARGB" to "ABGR" swaps R and B.
        if (absl::Status status = CheckStride(src, row_bytes); !status.ok()) return status;
        if (libyuv::ARGBToABGR(src.data, static_cast<int>(src.stride), dst.Data(),
                               static_cast<int>(dst.Stride()),
                               static_cast<int>(src.dimensions.width),
                               static_cast<int>(src.dimensions.height)) != 0) {
            return absl::InternalError("libyuv::ARGBToABGR failed");
        }
        return absl::OkStatus();
    default:
        return ConvertWithPixman(src, dst);
    }
}

}  // namespace

absl::StatusOr<std::unique_ptr<Slot>> CpuSlotStorage::Allocate(Dimensions size) {
    const uint64_t row_bytes = uint64_t{size.width} * kBytesPerPixel;
    const auto alignment = static_cast<uint64_t>(kRowAlignment);
    const uint64_t stride = (row_bytes + alignment - 1) / alignment * alignment;
    if (size.width == 0 || size.height == 0 || stride > kMaxBytes ||
        stride * size.height > kMaxBytes) {
        return absl::InvalidArgumentError(
                absl::StrFormat("Cannot allocate a %dx%d slot", size.width, size.height));
    }
    std::unique_ptr<CpuSlotBuffer> buffer = CpuSlotBuffer::Create(size, stride);
    if (buffer == nullptr) {
        return absl::ResourceExhaustedError(
                absl::StrFormat("Out of memory for a %dx%d slot", size.width, size.height));
    }
    return buffer;
}

void CpuSlotStorage::Capture(Slot& dst, const SurfaceSource& src,
                             absl::AnyInvocable<void(CaptureResult) &&> done) {
    TRACE_EVENT("display", "CpuSlotStorage::Capture", "w", dst.size().width, "h", dst.size().height,
                "bytes", static_cast<uint64_t>(dst.size().height) * src.stride);
    CaptureResult result;
    // A source only hands a storage the buffers it allocated.
    result.status = CopySurface(src, static_cast<CpuSlotBuffer&>(dst));
    if (result.status.ok()) result.dimensions = dst.size();
    std::move(done)(std::move(result));
}

}  // namespace goldfish::display
