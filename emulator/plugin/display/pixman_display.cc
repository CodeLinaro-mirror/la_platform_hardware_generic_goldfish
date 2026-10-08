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

// Some general notes on debug levels:
// VLOG(1) -- Get FPS from qemu
// VLOG(2) -- Get scaling and timing information
// VLOG(3) -- Add "blue" blocks in the corner for inspecting visual scaling issues.

#include "goldfish/display/pixman_display.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numeric>
#include <utility>

#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/display/cpu_slot_storage.h"
#include "goldfish/display/slot_storage.h"
#include "goldfish/perfetto/perfetto_categories.h"

extern "C" {
// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "pixman.h"
// IWYU pragma: end_keep
// clang-format on
}

namespace goldfish::display {

namespace {

constexpr bool kNoScaling = false;

// Helper function to compute the greatest common divisor.
// Used to simplify the scaling fraction.
int Gcd(int a, int b) {
    return std::abs(std::gcd(a, b));
}

// The maximum denominator allowed for a "safe" scaling ratio.
// Ratios with simplified denominators larger than this are likely to cause
// cumulative rounding errors in pixman's fixed-point arithmetic, leading to
// visual artifacts like shearing. A threshold of 1024 is sufficient to prevent
// visible rounding errors in 16.16 fixed-point math while allowing for
// much closer fits to the desired dimensions.
constexpr int kMaxSafeDenominator = 1024;

// Checks if a scaling operation from a source dimension to a target dimension
// is likely to be safe from precision-related artifacts.
bool IsScalingSafe(int source, int target) {
    if (target % 4) {
        // Pixman requires the stride (in bytes) to be a mulple of 4 bytes,
        // so for RGB (3 bytes per pixel) we need the width to be a multiple
        // of 4 as well.
        return false;
    }
    if (source == 0 || target == 0) return true;  // No scaling.
    const int common = Gcd(source, target);
    const int denominator = target / common;
    return denominator <= kMaxSafeDenominator;
}

}  // namespace

PixmanDisplay::PixmanDisplay(EventLoop* loop, int id, ::pixman_image_t* image, uint32_t dpi,
                             uint32_t flags)
        : PixmanDisplay(loop, /*executor=*/loop, id, image, dpi, flags) {
    UpdateSourceImage(image);
}

PixmanDisplay::PixmanDisplay(EventLoop* loop, EventLoop* executor, int id, ::pixman_image_t* image,
                             uint32_t dpi, uint32_t flags)
        : IDisplay(loop, id, image ? static_cast<uint32_t>(pixman_image_get_width(image)) : 0,
                   image ? static_cast<uint32_t>(pixman_image_get_height(image)) : 0, dpi, flags)
        , frame_producer_(std::make_shared<FrameProducer>(executor, frames()))
        // The hooks may use `this`: only the producer calls them, and
        // ShutdownProducer, first in the most-derived destructor, waits for
        // such a call and stops new ones.
        , legacy_(LegacySurfaceSource::Create(
                  frame_producer_,
                  LegacySurfaceSource::Hooks{
                      .refresh = [this] { RefreshSource(); },
                      .demand_changed = [this](bool warm) { OnSourceDemandChanged(warm); },
                      .capture =
                              [this](LegacySurfaceSource& source) { return CaptureSource(source); },
                  },
                  std::make_unique<CpuSlotStorage>())) {
    if (image != nullptr) {
        const absl::MutexLock lock(&pixman_mutex_);
        source_image_ = SharePixmanImage(image);
    }
    frame_producer_->SetSource(legacy_);
}

PixmanDisplay::~PixmanDisplay() {
    ShutdownProducer();
}

void PixmanDisplay::ShutdownProducer() {
    StopProducerCalls();  // OnDemand and OnFrameRequested forward to frame_producer_.
    frame_producer_->Shutdown();
}

PixmanDisplay::PixmanDisplay(EventLoop* loop, int id, const PixmanImagePtr& image, uint32_t dpi,
                             uint32_t flags)
        : PixmanDisplay(loop, id, image.get(), dpi, flags) {}

SurfaceSource PixmanDisplay::SurfaceSourceFromPixman(::pixman_image_t* image) {
    if (image == nullptr) {
        return {};
    }
    return SurfaceSource{
        .data = reinterpret_cast<const uint8_t*>(pixman_image_get_data(image)),
        .dimensions =
                {
                    .width = static_cast<uint32_t>(pixman_image_get_width(image)),
                    .height = static_cast<uint32_t>(pixman_image_get_height(image)),
                },
        .stride = static_cast<size_t>(pixman_image_get_stride(image)),
        .pixman_format = static_cast<uint32_t>(pixman_image_get_format(image)),
    };
}

void PixmanDisplay::UpdateSourceImage(::pixman_image_t* image) {
    DLOG_FIRST_N(WARNING, 2) << "--- WARNING! Reduced performance in debug builds ---";
    if (image == nullptr) {
        SetDimensions(0, 0);
        const absl::MutexLock lock(&pixman_mutex_);
        source_image_.reset();
        return;
    }

    const Dimensions dims = GetDimensions();
    // Used to debug issues around scaling, it will create a set of rotating color blocks
    // in the corners that you can use to visually analyze if things "look okay".
    // enable by setting the --vmodule "PixmanDisplay.*=3"
    if (ABSL_VLOG_IS_ON(3)) {
        LOG_FIRST_N(WARNING, 5) << "Adding rotating color blocks in the corners to visually "
                                   "diagnose frame ordering issues.";
        if (dims.width >= 100 && dims.height >= 100) {
            const uint64_t frame = CurrentFrameId();
            const pixman_color_t colors[4] = {
                {0xffff, 0, 0, 0xffff},       // Red
                {0, 0xffff, 0, 0xffff},       // Green
                {0, 0, 0xffff, 0xffff},       // Blue
                {0xffff, 0xffff, 0, 0xffff},  // Yellow
            };

            const pixman_rectangle16_t rects[4] = {
                {0, 0, 100, 100},                                        // Top-left
                {static_cast<int16_t>(dims.width - 100), 0, 100, 100},   // Top-right
                {0, static_cast<int16_t>(dims.height - 100), 100, 100},  // Bottom-left
                {static_cast<int16_t>(dims.width - 100), static_cast<int16_t>(dims.height - 100),
                 100, 100}  // Bottom-right
            };

            for (int i = 0; i < 4; ++i) {
                pixman_image_fill_rectangles(PIXMAN_OP_SRC, image, &colors[(frame + i) % 4], 1,
                                             &rects[i]);
            }
        }
    }

    const uint32_t new_width = static_cast<uint32_t>(pixman_image_get_width(image));
    const uint32_t new_height = static_cast<uint32_t>(pixman_image_get_height(image));
    SetDimensions(new_width, new_height);
    {
        const absl::MutexLock lock(&pixman_mutex_);
        source_image_ = SharePixmanImage(image);
        if (new_width > 0 && new_height > 0) {
            legacy_->Resize({.width = new_width, .height = new_height});
        }
    }
    VLOG(2) << "updateSourceImage: " << *this << " to: " << image;
}

void PixmanDisplay::UpdateSurface(int x, int y, int width, int height) {
    TRACE_EVENT("display", "PixmanDisplay::UpdateSurface", "x", x, "y", y, "w", width, "h", height);
    VLOG(2) << "UpdateSurface " << *this << ", to: (" << x << ", " << y << "), (" << width << "x"
            << height << ")";
    bool has_content;
    {
        const absl::MutexLock lock(&pixman_mutex_);
        has_content = source_image_ && pixman_image_get_data(source_image_.get()) != nullptr;
    }
    if (has_content) frame_producer_->Invalidate();
    if (ABSL_VLOG_IS_ON(2)) {
        fps_calculator_.AddFrame();
        VLOG_EVERY_N_SEC(2, 1) << "Qemu framerate: " << fps_calculator_.GetFps() << " fps";
    }
}

void PixmanDisplay::OnDemand(bool warm) {
    frame_producer_->SetDemand(warm);
}

void PixmanDisplay::OnFrameRequested() {
    frame_producer_->RequestFrame();
}

IFrameSource::Start PixmanDisplay::CaptureSource(LegacySurfaceSource& source) {
    // Held across the capture: it copies from source_image_'s pixels.
    const absl::MutexLock lock(&pixman_mutex_);
    if (source_image_ && pixman_image_get_data(source_image_.get()) != nullptr) {
        return source.CaptureFrom(SurfaceSourceFromPixman(source_image_.get()));
    }
    return IFrameSource::Start::kNoContent;
}

std::pair<int, int> PixmanDisplay::ResizeKeepAspectRatio(int desired_width, int desired_height) {
    auto fit = CalculateLogicalFit(desired_width, desired_height);
    if (fit.width == 0 || fit.height == 0) {
        return {0, 0};
    }

    // fast pass: no scaling
    // just return the original w and h, or swap them if
    // necessary (for 90 and 270 rotation)

    const Dimensions dims = GetDimensions();
    if (kNoScaling) {
        if (fit.swapped) {
            return {static_cast<int>(dims.height), static_cast<int>(dims.width)};
        }
        return {static_cast<int>(dims.width), static_cast<int>(dims.height)};
    }

    // The physical width of the destination buffer is always fit.width.
    // To ensure scaling is safe, we check the ratio between the source physical
    // stride and this destination physical width.
    const int source_physical_stride = static_cast<int>(fit.swapped ? dims.height : dims.width);
    int safe_width = fit.width;

    if (!IsScalingSafe(source_physical_stride, safe_width)) {
        for (int w_check = safe_width; w_check > 0; --w_check) {
            if (IsScalingSafe(source_physical_stride, w_check)) {
                safe_width = w_check;
                break;
            }
        }
    }

    // Logically oriented source dimensions.
    const int64_t s_width = fit.swapped ? dims.height : dims.width;
    const int64_t s_height = fit.swapped ? dims.width : dims.height;

    // Recalculate the logical height based on the safe logical width to
    // maintain the aspect ratio.
    const int safe_height = static_cast<int>((s_height * safe_width) / s_width);

    VLOG(2) << "Requested " << desired_width << "x" << desired_height << ", ideal " << fit.width
            << "x" << fit.height << ", snapped to safe " << safe_width << "x" << safe_height;

    return {safe_width, safe_height};
}

}  // namespace goldfish::display
