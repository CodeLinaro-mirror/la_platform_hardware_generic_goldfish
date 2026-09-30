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

#include "goldfish/display/display.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

#include "goldfish/display/write_png.h"

namespace goldfish::display {

Orientation IDisplay::GetOrientation(int w, int h) {
    if (w > h) return Orientation::kLandscape;
    if (h > w) return Orientation::kPortrait;
    return Orientation::kSquare;
}

IDisplay::LogicalFit IDisplay::CalculateLogicalFit(int desired_width, int desired_height) const {
    const Dimensions dims = GetDimensions();
    if (dims.width <= 0 || dims.height <= 0 || desired_width <= 0 || desired_height <= 0) {
        return {.width = 0, .height = 0, .swapped = false};
    }

    // We determine the source orientation based on the requested box.
    // If the box is rectangular (not square) and its orientation differs
    // from the physical display, we swap the source dimensions to calculate
    // the logical fit. Square sources or square boxes do not trigger a swap.
    const Orientation box_ori = GetOrientation(desired_width, desired_height);
    const Orientation source_ori =
            GetOrientation(static_cast<int>(dims.width), static_cast<int>(dims.height));

    const bool swapped =
            (box_ori == Orientation::kLandscape && source_ori == Orientation::kPortrait) ||
            (box_ori == Orientation::kPortrait && source_ori == Orientation::kLandscape);

    const int64_t s_width = swapped ? dims.height : dims.width;
    const int64_t s_height = swapped ? dims.width : dims.height;

    // Note that we will never scale above logical display device width and height.
    desired_width = static_cast<int>(std::min<int64_t>(desired_width, s_width));
    desired_height = static_cast<int>(std::min<int64_t>(desired_height, s_height));

    if (static_cast<int64_t>(desired_width) * s_height <
        static_cast<int64_t>(desired_height) * s_width) {
        // Width is the limiting factor.
        const int new_height = static_cast<int>((s_height * desired_width) / s_width);
        return {.width = desired_width, .height = new_height, .swapped = swapped};
    }  // Height is the limiting factor.
    const int new_width = static_cast<int>((s_width * desired_height) / s_height);
    return {.width = new_width, .height = desired_height, .swapped = swapped};
}

std::pair<int, int> IDisplay::ResizeKeepAspectRatio(int desired_width, int desired_height) {
    auto fit = CalculateLogicalFit(desired_width, desired_height);
    return {fit.width, fit.height};
}

absl::StatusOr<FrameInfo> IDisplay::GetPixels(PixelFormat fmt, int width, int height,
                                              ImageRotation rotation, uint8_t* pixel,
                                              size_t* c_pixels) const {
    if (c_pixels == nullptr) {
        return absl::InvalidArgumentError("c_pixels must not be null");
    }
    if (!frames_) {
        return absl::UnavailableError("Display has no frame source");
    }

    FramePtr frame;
    auto fresh = WaitForFreshFrame(absl::Now() + absl::Milliseconds(500));
    if (fresh.ok()) {
        frame = std::move(*fresh);
    } else if (absl::IsDeadlineExceeded(fresh.status())) {
        LatestFrame latest = AcquireLatestFrame();
        if (latest.removed) {
            return absl::NotFoundError("Display removed");
        }
        frame = std::move(latest.frame);
    } else {
        return fresh.status();
    }

    if (!frame) {
        return absl::UnavailableError("No frame has been produced yet.");
    }

    Dimensions output{0, 0};
    if (width > 0 && height > 0) {
        output = {.width = static_cast<uint32_t>(width), .height = static_cast<uint32_t>(height)};
    }
    const ReadRequest req{.rotation = rotation, .output = output};
    Dimensions out_dims{0, 0};

    if (fmt == PixelFormat::kPng) {
        std::vector<uint8_t> rgba;
        auto dims = frame->ReadInto(
                req, [&](const ReadLayout& layout) -> absl::StatusOr<Rgba8888Target> {
                    rgba.resize(layout.total_bytes);
                    return Rgba8888Target{.plane = Plane{.data = absl::MakeSpan(rgba)}};
                });
        if (!dims.ok()) {
            return dims.status();
        }
        out_dims = *dims;
        std::vector<uint8_t> png_bytes;
        if (!write_png(4, static_cast<int>(out_dims.width), static_cast<int>(out_dims.height),
                       rgba.data(), png_bytes)) {
            return absl::UnavailableError("Failed to create PNG screenshot!");
        }
        if (pixel == nullptr || *c_pixels < png_bytes.size()) {
            const size_t old = *c_pixels;
            *c_pixels = png_bytes.size();
            return absl::FailedPreconditionError(absl::StrFormat(
                    "Buffer too small; need %u bytes, have %u", png_bytes.size(), old));
        }
        std::memcpy(pixel, png_bytes.data(), png_bytes.size());
        *c_pixels = png_bytes.size();
    } else {
        const ReadFormat read_fmt =
                (fmt == PixelFormat::kRgb888) ? ReadFormat::kRgb888 : ReadFormat::kRgba8888;
        auto dims = frame->ReadInto(
                req, read_fmt, [&](const ReadLayout& layout) -> absl::StatusOr<uint8_t*> {
                    if (pixel == nullptr || *c_pixels < layout.total_bytes) {
                        const size_t old = *c_pixels;
                        *c_pixels = layout.total_bytes;
                        return absl::FailedPreconditionError(
                                absl::StrFormat("Buffer too small; need %u bytes, have %u",
                                                layout.total_bytes, old));
                    }
                    *c_pixels = layout.total_bytes;
                    return pixel;
                });
        if (!dims.ok()) {
            return dims.status();
        }
        out_dims = *dims;
    }

    const FrameEvent& ev = frame->event();
    FrameInfo info;
    info.dimensions = out_dims;
    info.format = fmt;
    info.rotation = rotation;
    info.sequence_number = ev.frame_id;
    info.timestamp = (ev.capture_time == absl::InfinitePast()) ? absl::Now() : ev.capture_time;
    return info;
}

std::string IDisplay::String() const {
    const Dimensions dims = GetDimensions();
    return absl::StrFormat("Display: %d (%dx%d), seq: %u", display_id_, dims.width, dims.height,
                           CurrentFrameId());
}

}  // namespace goldfish::display
