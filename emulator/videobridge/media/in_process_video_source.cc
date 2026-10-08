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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/video/video_frame.h"
#include "rtc_base/time_utils.h"
#pragma clang diagnostic pop

#include "absl/log/log.h"
#include "absl/types/span.h"

#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/display/abstract_multi_display.h"
#include "goldfish/display/frame.h"
#include "goldfish/display/geometry.h"
#include "goldfish/perfetto/perfetto_categories.h"

namespace goldfish::videobridge {

InProcessVideoSource::InProcessVideoSource(::goldfish::display::IMultiDisplay& multidisplay,
                                           uint32_t display_id)
        : multidisplay_(multidisplay), display_id_(display_id) {}

InProcessVideoSource::~InProcessVideoSource() {
    OnStop();
}

void InProcessVideoSource::OnStart() {
    OnStop();

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
    auto worker = ::goldfish::async::ThreadedEventLoop::Create(
            ::goldfish::async::LibuvEventLoop::Create());
    auto pump = std::make_shared<::goldfish::display::LatestFramePump>(
            [display]() { return display; }, worker.get(),
            [this](::goldfish::display::FramePtr frame) { ProcessFrame(std::move(frame)); });

    {
        const absl::MutexLock lock(&frame_mutex_);
        display_ = display;
        worker_ = std::move(worker);
        pump_ = pump;
        last_seq_ = 0;
        total_dropped_ = 0;
    }

    auto listener = display->AddFrameListener(
            [pump](const ::goldfish::display::FrameEvent& event) { pump->OnEvent(event); });

    if (auto latest = display->AcquireLatestFrame(); latest.frame) {
        pump->OnEvent(latest.frame->event());
    }

    const absl::MutexLock lock(&frame_mutex_);
    listener_ = std::move(listener);
}

void InProcessVideoSource::OnStop() {
    VLOG(1) << "Stopping InProcessVideoSource.";
    ::goldfish::display::IDisplay::ScopedFrameListener listener;
    std::shared_ptr<::goldfish::display::LatestFramePump> pump;
    std::unique_ptr<::goldfish::async::EventLoop> worker;
    std::shared_ptr<::goldfish::display::IDisplay> display;
    {
        const absl::MutexLock lock(&frame_mutex_);
        listener = std::move(listener_);
        pump = std::move(pump_);
        worker = std::move(worker_);
        display = std::move(display_);
        last_seq_ = 0;
        total_dropped_ = 0;
    }
    listener.reset();
    worker.reset();
    pump.reset();
    display.reset();
    buffer_pool_.Release();
}

void InProcessVideoSource::ProcessFrame(::goldfish::display::FramePtr frame) {
    if (!frame) {
        return;
    }

    const auto dims = frame->dimensions();
    const uint32_t even_width = dims.width & ~1U;
    const uint32_t even_height = dims.height & ~1U;
    if (even_width < 2 || even_height < 2) {
        return;
    }

    const uint64_t frame_seq = frame->event().frame_id;
    TRACE_EVENT("display", "InProcessVideoSource::ProcessFrame", "seq", frame_seq);
    {
        absl::MutexLock lock(&frame_mutex_);
        if (!display_) {
            return;
        }
        if (TRACE_EVENT_CATEGORY_ENABLED("display")) {
            if (last_seq_ != 0 && frame_seq > last_seq_ + 1) {
                const uint64_t dropped = frame_seq - last_seq_ - 1;
                total_dropped_ += dropped;
                TRACE_COUNTER("display", "webrtc_dropped_frames", total_dropped_);
                TRACE_EVENT_INSTANT("display", "WebRTCFrameDropped", "dropped_count", dropped,
                                    "last_seq", last_seq_, "current_seq", frame_seq);
            }
            last_seq_ = frame_seq;
        }
    }

    const ::goldfish::display::ReadRequest req{
        .rotation = ::goldfish::display::ImageRotation::kRotation0,
        .output = {0, 0},
    };
    ::webrtc::scoped_refptr<::webrtc::NV12BufferInterface> nv12_buffer;
    const auto read_res = frame->ReadInto(
            req,
            [&](const ::goldfish::display::ReadLayout& layout)
                    -> absl::StatusOr<::goldfish::display::Nv12Target> {
                const int w = static_cast<int>(layout.dimensions.width);
                const int h = static_cast<int>(layout.dimensions.height);
                auto buf = buffer_pool_.CreateNV12Buffer(w, h);
                if (!buf) {
                    buf = ::webrtc::NV12Buffer::Create(w, h);
                }
                const size_t y_bytes = static_cast<size_t>(buf->StrideY()) * h;
                const size_t uv_bytes = static_cast<size_t>(buf->StrideUV()) * (h / 2);
                ::goldfish::display::Nv12Target target{
                    .y =
                            ::goldfish::display::Plane{
                                .data = absl::MakeSpan(buf->MutableDataY(), y_bytes),
                                .stride = static_cast<size_t>(buf->StrideY()),
                            },
                    .uv =
                            ::goldfish::display::Plane{
                                .data = absl::MakeSpan(buf->MutableDataUV(), uv_bytes),
                                .stride = static_cast<size_t>(buf->StrideUV()),
                            },
                };
                nv12_buffer = std::move(buf);
                return target;
            });
    if (!read_res.ok()) {
        LOG(ERROR) << "InProcessVideoSource failed to read frame into NV12 buffer.";
        return;
    }

    const int64_t timestamp_us = ::webrtc::TimeMicros();
    const ::webrtc::VideoRotation webrtc_rotation = ToWebRtcRotation(frame->rotation());

    ::webrtc::VideoFrame video_frame = ::webrtc::VideoFrame::Builder()
                                               .set_video_frame_buffer(nv12_buffer)
                                               .set_timestamp_rtp(0)
                                               .set_timestamp_us(timestamp_us)
                                               .set_rotation(webrtc_rotation)
                                               .build();

    {
        TRACE_EVENT("display", "InProcessVideoSource::OnFrame",
                    perfetto::Flow::ProcessScoped(frame_seq), "seq", frame_seq);
        this->OnFrame(video_frame);
    }
}

::webrtc::VideoRotation ToWebRtcRotation(goldfish::display::ImageRotation rotation) {
    // Note: Goldfish ImageRotation is counter-clockwise (CCW), whereas
    // webrtc::VideoRotation (RTP CVO) is clockwise (CW).
    switch (rotation) {
    case goldfish::display::ImageRotation::kRotation90:
        return ::webrtc::kVideoRotation_270;
    case goldfish::display::ImageRotation::kRotation180:
        return ::webrtc::kVideoRotation_180;
    case goldfish::display::ImageRotation::kRotation270:
        return ::webrtc::kVideoRotation_90;
    case goldfish::display::ImageRotation::kRotation0:
    default:
        return ::webrtc::kVideoRotation_0;
    }
}

}  // namespace goldfish::videobridge
