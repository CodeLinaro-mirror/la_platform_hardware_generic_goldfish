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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/video/video_frame.h"
#include "libyuv/convert_from_argb.h"
#include "rtc_base/time_utils.h"
#pragma clang diagnostic pop

#include "absl/log/log.h"

#include "goldfish/display/QemuMultidisplay/multi_display.h"

namespace goldfish::videobridge {

InProcessVideoSource::InProcessVideoSource(::goldfish::display::IMultiDisplay& multidisplay,
                                           uint32_t display_id)
        : multidisplay_(multidisplay), display_id_(display_id) {}

InProcessVideoSource::~InProcessVideoSource() {
    OnStop();
}

void InProcessVideoSource::OnStart() {
    std::shared_ptr<::goldfish::display::IDisplay> display;
    auto display_res = multidisplay_.GetDisplay(display_id_);
    if (display_res.ok()) {
        display = display_res.value().lock();
    }

    if (!display) {
        LOG(WARNING) << "InProcessVideoSource: Display " << display_id_
                     << " is unavailable; WebRTC video track will remain idle.";
        return;
    }

    VLOG(1) << "Starting InProcessVideoSource for display " << static_cast<int>(display->Id());
    auto* callback_source =
            static_cast<::goldfish::display::FrameInfoCallbackSource*>(display.get());
    auto sub = android::base::eventing::MakeScopedCallback(
            *callback_source, [this](const ::goldfish::display::FrameInfo& frame_info) {
                OnFrameAvailable(frame_info);
            });

    absl::MutexLock lock(&frame_mutex_);
    display_ = std::move(display);
    subscription_ = std::move(sub);
}

void InProcessVideoSource::OnStop() {
    VLOG(1) << "Stopping InProcessVideoSource.";
    std::unique_ptr<android::base::eventing::ScopedEventCallback<
            ::goldfish::display::FrameInfoCallbackSource, ::goldfish::display::FrameInfo>>
            sub;
    {
        absl::MutexLock lock(&frame_mutex_);
        sub = std::move(subscription_);
        display_.reset();
    }
}

void InProcessVideoSource::OnFrameAvailable(const ::goldfish::display::FrameInfo& frame_info) {
    if (!frame_info.pixels || frame_info.dimensions.width == 0 ||
        frame_info.dimensions.height == 0) {
        return;
    }

    const uint32_t even_width = frame_info.dimensions.width & ~1U;
    const uint32_t even_height = frame_info.dimensions.height & ~1U;
    if (even_width == 0 || even_height == 0) {
        return;
    }

    {
        absl::MutexLock lock(&frame_mutex_);
        if (!display_) {
            return;
        }
    }

    auto nv12_buffer = ::webrtc::NV12Buffer::Create(static_cast<int>(even_width),
                                                    static_cast<int>(even_height));

    const int cvt_res = libyuv::ABGRToNV12(
            frame_info.pixels, static_cast<int>(frame_info.stride), nv12_buffer->MutableDataY(),
            nv12_buffer->StrideY(), nv12_buffer->MutableDataUV(), nv12_buffer->StrideUV(),
            static_cast<int>(even_width), static_cast<int>(even_height));

    if (cvt_res != 0) {
        LOG(ERROR) << "InProcessVideoSource failed to convert frame to NV12 format.";
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
                                               .set_video_frame_buffer(nv12_buffer)
                                               .set_timestamp_rtp(0)
                                               .set_timestamp_us(timestamp_us)
                                               .set_rotation(webrtc_rotation)
                                               .build();

    this->OnFrame(video_frame);
}

}  // namespace goldfish::videobridge
