// Copyright (C) 2026 The Android Open Source Project
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

#include "goldfish/videobridge/in_process_video_source.h"

#include <utility>

#include "absl/log/log.h"

#include "api/video/video_frame.h"
#include "libyuv.h"
#include "rtc_base/time_utils.h"

namespace goldfish::videobridge {

InProcessVideoSource::InProcessVideoSource(std::shared_ptr<::goldfish::display::IDisplay> display)
        : display_(std::move(display)) {}

InProcessVideoSource::~InProcessVideoSource() {
    Stop();
}

void InProcessVideoSource::Start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }
    if (!display_) {
        LOG(ERROR) << "InProcessVideoSource cannot start: display is null.";
        return;
    }

    VLOG(1) << "Starting InProcessVideoSource for display " << static_cast<int>(display_->Id());
    auto* callback_source =
            static_cast<::goldfish::display::FrameInfoCallbackSource*>(display_.get());
    subscription_ = android::base::eventing::MakeScopedCallback(
            *callback_source, [this](const ::goldfish::display::FrameInfo& frame_info) {
                OnFrameAvailable(frame_info);
            });
}

void InProcessVideoSource::Stop() {
    bool expected = true;
    if (running_.compare_exchange_strong(expected, false)) {
        VLOG(1) << "Stopping InProcessVideoSource.";
        subscription_.reset();
    }
}

void InProcessVideoSource::OnFrameAvailable(const ::goldfish::display::FrameInfo& frame_info) {
    if (!running_ || !display_ || !frame_info.pixels || frame_info.dimensions.width == 0 ||
        frame_info.dimensions.height == 0) {
        return;
    }

    const uint32_t even_width = frame_info.dimensions.width & ~1u;
    const uint32_t even_height = frame_info.dimensions.height & ~1u;
    if (even_width == 0 || even_height == 0) {
        return;
    }

    if (!i420_buffer_ || i420_buffer_->width() != static_cast<int>(even_width) ||
        i420_buffer_->height() != static_cast<int>(even_height)) {
        i420_buffer_ = ::webrtc::I420Buffer::Create(static_cast<int>(even_width),
                                                    static_cast<int>(even_height));
    }

    const int cvt_res = libyuv::ABGRToI420(
            frame_info.pixels, static_cast<int>(frame_info.stride), i420_buffer_->MutableDataY(),
            i420_buffer_->StrideY(), i420_buffer_->MutableDataU(), i420_buffer_->StrideU(),
            i420_buffer_->MutableDataV(), i420_buffer_->StrideV(), static_cast<int>(even_width),
            static_cast<int>(even_height));

    if (cvt_res != 0) {
        LOG(ERROR) << "InProcessVideoSource failed to convert frame to I420 format.";
        return;
    }

    const int64_t timestamp_us = ::webrtc::TimeMicros();
    ::webrtc::VideoRotation webrtc_rotation = ::webrtc::kVideoRotation_0;
    switch (frame_info.rotation) {
    case goldfish::display::ImageRotation::kRotation90:
        webrtc_rotation = ::webrtc::kVideoRotation_90;
        break;
    case goldfish::display::ImageRotation::kRotation180:
        webrtc_rotation = ::webrtc::kVideoRotation_180;
        break;
    case goldfish::display::ImageRotation::kRotation270:
        webrtc_rotation = ::webrtc::kVideoRotation_270;
        break;
    case goldfish::display::ImageRotation::kRotation0:
    default:
        webrtc_rotation = ::webrtc::kVideoRotation_0;
        break;
    }

    ::webrtc::VideoFrame video_frame = ::webrtc::VideoFrame::Builder()
                                               .set_video_frame_buffer(i420_buffer_)
                                               .set_timestamp_rtp(0)
                                               .set_timestamp_us(timestamp_us)
                                               .set_rotation(webrtc_rotation)
                                               .build();

    this->OnFrame(video_frame);
}

}  // namespace goldfish::videobridge
