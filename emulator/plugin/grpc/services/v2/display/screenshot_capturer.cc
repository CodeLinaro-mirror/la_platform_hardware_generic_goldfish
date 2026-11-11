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

#include "display/screenshot_capturer.h"

#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

#include "android/emulation/control/absl_status_translate.h"
#include "android/emulation/control/simple_async_grpc.h"
#include "goldfish/async/libuv_event_loop.h"
#include "goldfish/async/threaded_event_loop.h"
#include "goldfish/display/cpu_read_pipeline.h"
#include "goldfish/display/display_frames.h"
#include "goldfish/display/latest_frame_pump.h"
#include "goldfish/display/write_jpeg.h"
#include "goldfish/display/write_png.h"

namespace goldfish::grpc::v2 {
namespace {

using ::android::emulation::control::AbslStatusToGrpcStatus;
using ::android::emulation::v2::GetScreenshotRequest;
using ::android::emulation::v2::ImageFormat;
using ::android::emulation::v2::Rotation;
using ::android::emulation::v2::Screenshot;
using ::android::emulation::v2::StreamScreenshotsRequest;
using ::goldfish::display::Dimensions;
using ::goldfish::display::FrameEvent;
using ::goldfish::display::FramePtr;
using ::goldfish::display::IDisplay;
using ::goldfish::display::ImageRotation;
using ::goldfish::display::LatestFrame;
using ::goldfish::display::LatestFramePump;
using ::goldfish::display::Plane;
using ::goldfish::display::ReadFormat;
using ::goldfish::display::ReadLayout;
using ::goldfish::display::ReadRequest;
using ::goldfish::display::Rgba8888Target;
using ::goldfish::display::SurfaceFrame;

constexpr int kMaxScreenshotReadAttempts = 3;
constexpr float kBaselineDensityDpi = 160.0F;

using ScreenshotWriter =
        ::SimpleServerLambdaWriter<Screenshot, ::grpc::ServerWriteReactor<Screenshot>,
                                   /*max_queue_size=*/1, /*recycle_size=*/2>;

void DestroyTimerAsync(const std::shared_ptr<async::EventLoop>& loop,
                       async::EventLoop::ScopedTimer timer) {
    if (timer == nullptr || loop == nullptr) {
        return;
    }
    if (loop->IsOnLoopThread()) {
        timer.reset();
        return;
    }
    auto holder = std::make_shared<async::EventLoop::ScopedTimer>(std::move(timer));
    if (!loop->Post([holder]() mutable { holder->reset(); }).ok()) {
        holder->reset();
    }
}

}  // namespace

struct ScreenshotCapturer::SharedState {
    SharedState(int32_t default_dpi, const system::SteadyClock& steady_clock)
            : default_density_dpi(default_dpi)
            , clock(steady_clock)
            , worker_loop(std::shared_ptr<async::EventLoop>(
                      async::ThreadedEventLoop::Create(async::LibuvEventLoop::Create()))) {}

    absl::Status EncodeFrame(const IDisplay& display, const SurfaceFrame& frame, ImageFormat format,
                             Screenshot* out) const;

    const int32_t default_density_dpi;
    const system::SteadyClock& clock;
    std::shared_ptr<async::EventLoop> worker_loop;

    absl::Mutex streams_mu;
    std::vector<std::weak_ptr<StreamState>> active_streams ABSL_GUARDED_BY(streams_mu);
};

struct ScreenshotCapturer::StreamState
        : public std::enable_shared_from_this<ScreenshotCapturer::StreamState> {
    std::shared_ptr<SharedState> shared;
    std::shared_ptr<IDisplay> display;
    ImageFormat format = ImageFormat::IMAGE_FORMAT_PNG;
    absl::Duration min_interval = absl::ZeroDuration();

    absl::Mutex mu;
    ScreenshotWriter* writer ABSL_GUARDED_BY(mu) = nullptr;
    bool stopped ABSL_GUARDED_BY(mu) = false;
    std::optional<absl::Duration> next_allowed_at ABSL_GUARDED_BY(mu);
    FramePtr throttled_frame ABSL_GUARDED_BY(mu);
    async::EventLoop::ScopedTimer throttle_timer ABSL_GUARDED_BY(mu);
    std::shared_ptr<LatestFramePump> pump;
    IDisplay::ScopedFrameListener frame_listener ABSL_GUARDED_BY(mu);

    // Attaches `new_writer` and starts delivering frames. If the capturer was destroyed
    // first, finishes the RPC right away instead.
    void Start(ScreenshotWriter* new_writer) ABSL_LOCKS_EXCLUDED(mu) {
        {
            const absl::MutexLock lock(mu);
            if (stopped) {
                new_writer->Finish(ShuttingDownStatus());
                return;
            }
            writer = new_writer;
        }

        std::weak_ptr<StreamState> weak_self = shared_from_this();
        pump = std::make_shared<LatestFramePump>([disp = display]() { return disp; },
                                                 shared->worker_loop.get(),
                                                 [weak_self](FramePtr frame) {
                                                     if (auto self = weak_self.lock()) {
                                                         self->OnPumpFrame(std::move(frame));
                                                     }
                                                 },
                                                 [weak_self]() {
                                                     if (auto self = weak_self.lock()) {
                                                         self->OnDisplayRemoved();
                                                     }
                                                 });

        IDisplay::ScopedFrameListener listener = display->AddFrameListener(
                [frame_pump = pump](const FrameEvent& event) { frame_pump->OnEvent(event); });
        {
            const absl::MutexLock lock(mu);
            if (!stopped) {
                frame_listener = std::move(listener);
            }
        }
        // Still set if the stream stopped while attaching; drop it outside `mu`.
        listener.reset();

        if (display->AcquireLatestFrame().removed) {
            pump->OnEvent(FrameEvent{});
        }
    }

    void EmitFrame(const FramePtr& frame) ABSL_LOCKS_EXCLUDED(mu) {
        if (!frame) {
            return;
        }
        Screenshot msg;
        {
            const absl::MutexLock lock(mu);
            if (stopped || writer == nullptr) {
                return;
            }
            msg = writer->AcquireMessage();
        }

        const absl::Status status = shared->EncodeFrame(*display, *frame, format, &msg);
        if (absl::IsUnavailable(status)) {
            if (pump != nullptr) {
                pump->OnEvent(FrameEvent{});
            }
            return;
        }

        const absl::MutexLock lock(mu);
        if (!status.ok()) {
            FinishLocked(AbslStatusToGrpcStatus(status));
            return;
        }
        if (!stopped && writer != nullptr) {
            writer->Write(std::move(msg));
        }
    }

    void OnPumpFrame(FramePtr frame) ABSL_LOCKS_EXCLUDED(mu) {
        if (!frame || frame->event().blank) {
            return;
        }
        if (min_interval > absl::ZeroDuration()) {
            const absl::Duration now = shared->clock.Now();
            const absl::MutexLock lock(mu);
            if (stopped || writer == nullptr) {
                return;
            }
            if (next_allowed_at.has_value() && now < *next_allowed_at) {
                DeferFrameLocked(std::move(frame), *next_allowed_at - now);
                return;
            }
            throttled_frame.reset();
            next_allowed_at = now + min_interval;
        }
        EmitFrame(frame);
    }

    // Keeps only the newest throttled frame and schedules its delivery after `delay`.
    void DeferFrameLocked(FramePtr frame, absl::Duration delay) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu) {
        const bool already_scheduled = (throttled_frame != nullptr);
        throttled_frame = std::move(frame);
        if (already_scheduled) {
            return;
        }
        if (throttle_timer == nullptr) {
            std::weak_ptr<StreamState> weak_self = shared_from_this();
            throttle_timer = shared->worker_loop->CreateTimer([weak_self]() {
                if (auto self = weak_self.lock()) {
                    self->OnThrottleTimerFired();
                }
                return false;
            });
        }
        throttle_timer->Schedule(delay);
    }

    void OnThrottleTimerFired() ABSL_LOCKS_EXCLUDED(mu) {
        FramePtr frame;
        {
            const absl::MutexLock lock(mu);
            if (stopped || writer == nullptr || !throttled_frame) {
                return;
            }
            frame = std::move(throttled_frame);
            next_allowed_at = shared->clock.Now() + min_interval;
        }
        EmitFrame(frame);
    }

    void OnDisplayRemoved() ABSL_LOCKS_EXCLUDED(mu) {
        Stop(::grpc::Status(::grpc::StatusCode::NOT_FOUND,
                            absl::StrFormat("Failed to stream screenshots of display %u: display "
                                            "%u was removed; call ListDisplays to inspect "
                                            "remaining active displays.",
                                            display->Id(), display->Id())));
    }

    void OnCapturerDestroyed() ABSL_LOCKS_EXCLUDED(mu) { Stop(ShuttingDownStatus()); }

    // Cancel and done callback of the writer, when the RPC is already finishing.
    void Shutdown() ABSL_LOCKS_EXCLUDED(mu) { Stop(std::nullopt); }

  private:
    ::grpc::Status ShuttingDownStatus() const {
        return ::grpc::Status(
                ::grpc::StatusCode::UNAVAILABLE,
                absl::StrFormat("Screenshot stream for display %u terminated because the "
                                "screenshot service is shutting down; reconnect once the "
                                "service restarts.",
                                display->Id()));
    }

    // Stops the stream and finishes the RPC with `status` if it is still open. Finish() runs
    // under `mu` because OnDone takes `mu` (in Shutdown()) before it deletes the writer, so the
    // writer stays alive even if the client cancels at the same time.
    void FinishLocked(const ::grpc::Status& status) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu) {
        stopped = true;
        if (writer != nullptr) {
            std::exchange(writer, nullptr)->Finish(status);
        }
    }

    // Stops the stream, finishing the RPC with `status` when one is given. The frame listener
    // and timer are moved out under `mu` and destroyed after it is released, because destroying
    // the listener waits for a running callback.
    void Stop(const std::optional<::grpc::Status>& status) ABSL_LOCKS_EXCLUDED(mu) {
        IDisplay::ScopedFrameListener listener;
        async::EventLoop::ScopedTimer timer;
        {
            const absl::MutexLock lock(mu);
            listener = std::move(frame_listener);
            timer = std::move(throttle_timer);
            throttled_frame.reset();
            if (status.has_value()) {
                FinishLocked(*status);
            }
            stopped = true;
            writer = nullptr;
        }
        listener.reset();
        DestroyTimerAsync(shared->worker_loop, std::move(timer));
    }
};

Rotation ToProtoRotation(ImageRotation rotation) {
    switch (rotation) {
    case ImageRotation::kRotation0:
        return Rotation::ROTATION_DEG0;
    case ImageRotation::kRotation90:
        return Rotation::ROTATION_DEG90;
    case ImageRotation::kRotation180:
        return Rotation::ROTATION_DEG180;
    case ImageRotation::kRotation270:
        return Rotation::ROTATION_DEG270;
    }
    return Rotation::ROTATION_DEG0;
}

absl::StatusOr<ImageRotation> FromProtoRotation(Rotation rotation) {
    switch (rotation) {
    case Rotation::ROTATION_DEG0:
        return ImageRotation::kRotation0;
    case Rotation::ROTATION_DEG90:
        return ImageRotation::kRotation90;
    case Rotation::ROTATION_DEG180:
        return ImageRotation::kRotation180;
    case Rotation::ROTATION_DEG270:
        return ImageRotation::kRotation270;
    default:
        return absl::InvalidArgumentError(
                absl::StrFormat("Invalid Rotation value %d; specify ROTATION_DEG0, ROTATION_DEG90, "
                                "ROTATION_DEG180, or ROTATION_DEG270.",
                                static_cast<int>(rotation)));
    }
}

absl::StatusOr<ImageFormat> ResolveImageFormat(ImageFormat requested) {
    switch (requested) {
    case ImageFormat::IMAGE_FORMAT_UNSPECIFIED:
    case ImageFormat::IMAGE_FORMAT_PNG:
        return ImageFormat::IMAGE_FORMAT_PNG;
    case ImageFormat::IMAGE_FORMAT_RAW_RGBA8888:
        return ImageFormat::IMAGE_FORMAT_RAW_RGBA8888;
    case ImageFormat::IMAGE_FORMAT_JPEG:
        return ImageFormat::IMAGE_FORMAT_JPEG;
    default:
        return absl::InvalidArgumentError(
                absl::StrFormat("Unsupported screenshot ImageFormat %d; specify IMAGE_FORMAT_PNG, "
                                "IMAGE_FORMAT_RAW_RGBA8888, or IMAGE_FORMAT_JPEG.",
                                static_cast<int>(requested)));
    }
}

ScreenshotCapturer::ScreenshotCapturer(int32_t default_density_dpi,
                                       const system::SteadyClock& clock)
        : shared_(std::make_shared<SharedState>(default_density_dpi, clock)) {}

ScreenshotCapturer::~ScreenshotCapturer() {
    if (!shared_) {
        return;
    }
    std::vector<std::shared_ptr<StreamState>> streams;
    {
        const absl::MutexLock lock(shared_->streams_mu);
        streams.reserve(shared_->active_streams.size());
        for (const auto& weak_stream : shared_->active_streams) {
            if (auto locked = weak_stream.lock()) {
                streams.push_back(std::move(locked));
            }
        }
        shared_->active_streams.clear();
    }
    for (const auto& stream : streams) {
        stream->OnCapturerDestroyed();
    }
    if (shared_->worker_loop != nullptr && !shared_->worker_loop->IsOnLoopThread()) {
        shared_->worker_loop->PostAndWait([] {}).IgnoreError();
    }
}

absl::Status ScreenshotCapturer::SharedState::EncodeFrame(const IDisplay& display,
                                                          const SurfaceFrame& frame,
                                                          ImageFormat format,
                                                          Screenshot* out) const {
    const uint32_t display_id = display.Id();
    const Dimensions src_dims = frame.dimensions();
    if (src_dims.width == 0 || src_dims.height == 0) {
        return absl::UnavailableError(absl::StrFormat(
                "Failed to encode screenshot for display %u: frame dimensions are empty (%ux%u); "
                "retry after the guest compositor publishes a non-empty frame.",
                display_id, src_dims.width, src_dims.height));
    }

    const ImageRotation rotation = frame.rotation();
    const int32_t dpi =
            display.Dpi() > 0 ? static_cast<int32_t>(display.Dpi()) : default_density_dpi;

    const ReadRequest read_req{.rotation = rotation};
    Dimensions out_dims{0, 0};
    int32_t row_stride_bytes = 0;
    std::string* payload = out->mutable_image_data();

    if (format == ImageFormat::IMAGE_FORMAT_RAW_RGBA8888) {
        auto dims = frame.ReadInto(read_req, ReadFormat::kRgba8888,
                                   [&](const ReadLayout& layout) -> absl::StatusOr<uint8_t*> {
                                       payload->resize(layout.total_bytes);
                                       return reinterpret_cast<uint8_t*>(payload->data());
                                   });
        if (!dims.ok()) {
            return dims.status();
        }
        out_dims = *dims;
        row_stride_bytes = static_cast<int32_t>(out_dims.width) * 4;
    } else {
        std::optional<display::ScratchBuffer> rgba;
        auto dims = frame.ReadInto(
                read_req, [&](const ReadLayout& layout) -> absl::StatusOr<Rgba8888Target> {
                    rgba.emplace(display::AcquireScratchBuffer(layout.total_bytes));
                    return Rgba8888Target{
                        .plane = Plane{.data = absl::MakeSpan(rgba->data(), layout.total_bytes)}};
                });
        if (!dims.ok()) {
            return dims.status();
        }
        out_dims = *dims;

        if (format == ImageFormat::IMAGE_FORMAT_PNG) {
            thread_local std::vector<uint8_t> png_scratch;
            png_scratch.clear();
            const bool ok = display::write_png(4, out_dims.width, out_dims.height, rgba->data(),
                                               png_scratch);
            rgba.reset();
            if (!ok) {
                return absl::InternalError(absl::StrFormat(
                        "Failed to encode PNG screenshot for display %u (%ux%u): libpng write "
                        "failed; retry with IMAGE_FORMAT_RAW_RGBA8888 or inspect host memory.",
                        display_id, out_dims.width, out_dims.height));
            }
            payload->assign(reinterpret_cast<const char*>(png_scratch.data()), png_scratch.size());
        } else if (format == ImageFormat::IMAGE_FORMAT_JPEG) {
            const bool ok = display::EncodeRgbaToJpeg(out_dims.width, out_dims.height, rgba->data(),
                                                      payload);
            rgba.reset();
            if (!ok) {
                return absl::InternalError(absl::StrFormat(
                        "Failed to encode JPEG screenshot for display %u (%ux%u): JPEG encoder "
                        "failed; retry with IMAGE_FORMAT_PNG or IMAGE_FORMAT_RAW_RGBA8888.",
                        display_id, out_dims.width, out_dims.height));
            }
        }
    }

    const FrameEvent& ev = frame.event();
    const int64_t presentation_time_nanos = absl::ToInt64Nanoseconds(clock.Now());

    out->set_width(static_cast<int32_t>(out_dims.width));
    out->set_height(static_cast<int32_t>(out_dims.height));
    out->set_row_stride_bytes(row_stride_bytes);
    out->set_frame_sequence_number(static_cast<int64_t>(ev.frame_id));
    out->set_rotation(ToProtoRotation(rotation));
    out->set_presentation_time_nanos(presentation_time_nanos);
    out->set_density_scale(static_cast<float>(dpi) / kBaselineDensityDpi);
    return absl::OkStatus();
}

absl::Status ScreenshotCapturer::EncodeFrame(const IDisplay& display, const SurfaceFrame& frame,
                                             ImageFormat format, Screenshot* out) const {
    return shared_->EncodeFrame(display, frame, format, out);
}

::grpc::Status ScreenshotCapturer::CaptureScreenshot(const IDisplay& display,
                                                     const GetScreenshotRequest& request,
                                                     Screenshot* response) const {
    auto format = ResolveImageFormat(request.format());
    if (!format.ok()) {
        return AbslStatusToGrpcStatus(format.status());
    }

    if ((display.Flags() & kAndroidFlagSecure) != 0U) {
        return ::grpc::Status(
                ::grpc::StatusCode::FAILED_PRECONDITION,
                absl::StrFormat("Failed to capture screenshot of display %u: display has "
                                "DISPLAY_FLAG_SECURE set; capture a non-secure display or "
                                "recreate the virtual display without DISPLAY_FLAG_SECURE.",
                                display.Id()));
    }

    for (int attempt = 0; attempt < kMaxScreenshotReadAttempts; ++attempt) {
        FramePtr frame;
        auto fresh = display.WaitForFreshFrame(absl::Now() + absl::Milliseconds(500));
        if (fresh.ok()) {
            frame = std::move(*fresh);
        } else if (absl::IsDeadlineExceeded(fresh.status())) {
            LatestFrame latest = display.AcquireLatestFrame();
            if (latest.removed) {
                return ::grpc::Status(
                        ::grpc::StatusCode::NOT_FOUND,
                        absl::StrFormat("Failed to capture screenshot of display %u: display %u "
                                        "was removed during capture; call ListDisplays to inspect "
                                        "active displays.",
                                        display.Id(), display.Id()));
            }
            frame = std::move(latest.frame);
        } else {
            return AbslStatusToGrpcStatus(fresh.status());
        }

        if (!frame) {
            return ::grpc::Status(
                    ::grpc::StatusCode::UNAVAILABLE,
                    absl::StrFormat("Failed to capture screenshot of display %u: display has not "
                                    "produced a frame yet; retry after the guest compositor "
                                    "initializes.",
                                    display.Id()));
        }

        if (frame->event().blank) {
            return ::grpc::Status(
                    ::grpc::StatusCode::FAILED_PRECONDITION,
                    absl::StrFormat("Failed to capture screenshot of display %u: display is "
                                    "currently blanked with no active guest surface content; wake "
                                    "the display or wait for the guest compositor to render a "
                                    "frame before retrying.",
                                    display.Id()));
        }

        const absl::Status encode_status = EncodeFrame(display, *frame, *format, response);
        if (encode_status.ok()) {
            return ::grpc::Status::OK;
        }
        if (absl::IsUnavailable(encode_status) && attempt + 1 < kMaxScreenshotReadAttempts) {
            continue;
        }
        return AbslStatusToGrpcStatus(encode_status);
    }

    return ::grpc::Status(
            ::grpc::StatusCode::UNAVAILABLE,
            absl::StrFormat("Failed to capture screenshot of display %u after %d attempts because "
                            "the display producer was repeatedly reset; retry once the guest "
                            "display stabilizes.",
                            display.Id(), kMaxScreenshotReadAttempts));
}

::grpc::ServerWriteReactor<Screenshot>* ScreenshotCapturer::StreamScreenshots(
        std::shared_ptr<IDisplay> display, const StreamScreenshotsRequest& request) const {
    auto format = ResolveImageFormat(request.format());
    if (!format.ok()) {
        return new ::ErrorServerWriter<Screenshot>(AbslStatusToGrpcStatus(format.status()));
    }

    absl::Duration min_frame_interval = absl::ZeroDuration();
    if (request.has_max_frames_per_second()) {
        const float max_fps = request.max_frames_per_second();
        if (!std::isfinite(max_fps) || max_fps <= 0.0F) {
            return new ::ErrorServerWriter<Screenshot>(::grpc::Status(
                    ::grpc::StatusCode::INVALID_ARGUMENT,
                    absl::StrFormat("Invalid max_frames_per_second (%f) in "
                                    "StreamScreenshotsRequest; specify a finite value > 0.0 or "
                                    "omit max_frames_per_second for unthrottled delivery.",
                                    static_cast<double>(max_fps))));
        }
        min_frame_interval = absl::Seconds(1.0 / static_cast<double>(max_fps));
    }

    if ((display->Flags() & kAndroidFlagSecure) != 0U) {
        return new ::ErrorServerWriter<Screenshot>(::grpc::Status(
                ::grpc::StatusCode::FAILED_PRECONDITION,
                absl::StrFormat("Failed to stream screenshots of display %u: display has "
                                "DISPLAY_FLAG_SECURE set; stream from a non-secure display or "
                                "recreate the virtual display without DISPLAY_FLAG_SECURE.",
                                display->Id())));
    }

    auto state = std::make_shared<StreamState>();
    state->shared = shared_;
    state->display = std::move(display);
    state->format = *format;
    state->min_interval = min_frame_interval;

    {
        const absl::MutexLock lock(shared_->streams_mu);
        shared_->active_streams.erase(
                std::remove_if(shared_->active_streams.begin(), shared_->active_streams.end(),
                               [](const std::weak_ptr<StreamState>& w) { return w.expired(); }),
                shared_->active_streams.end());
        shared_->active_streams.push_back(state);
    }

    return new ScreenshotWriter([state](ScreenshotWriter* writer) { state->Start(writer); },
                                [state]() { state->Shutdown(); }, [state]() { state->Shutdown(); });
}

}  // namespace goldfish::grpc::v2
