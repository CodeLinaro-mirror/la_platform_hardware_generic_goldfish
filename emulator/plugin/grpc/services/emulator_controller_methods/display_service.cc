
// Copyright (C) 2024 The Android Open Source Project
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
#include "display_service.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "grpcpp/grpcpp.h"

#include "android/base/system.h"
#include "android/emulation/control/absl_status_translate.h"
#include "android/goldfish/hardware_config.h"
#include "goldfish/avd/avd_info.h"
#include "goldfish/devices/multidisplay/multidisplay_device.h"
#include "goldfish/display/display_frames.h"
#include "goldfish/display/frame.h"
#include "goldfish/display/geometry.h"
#include "goldfish/display/multi_display_callbacks.h"
#include "goldfish/display/pixman_display.h"
#include "goldfish/display/write_png.h"
#include "goldfish/fps_calculator.h"
#include "goldfish/memory/shared_memory.h"
#include "goldfish/perfetto/perfetto_categories.h"
#include "goldfish/physics/rotation.h"

namespace android {
namespace emulation {
namespace control {

using ::goldfish::display::FrameEvent;
using ::goldfish::display::FramePtr;
using ::goldfish::display::IDisplay;
using ::goldfish::display::ImageRotation;
using ::goldfish::display::IMultiDisplay;
using ::goldfish::display::PixelFormat;
using ::goldfish::display::Plane;
using ::goldfish::display::ReadFormat;
using ::goldfish::display::ReadLayout;
using ::goldfish::display::ReadRequest;
using ::goldfish::display::ReadTarget;
using ::goldfish::display::Rgb888Target;
using ::goldfish::display::Rgba8888Target;
using ::goldfish::display::ToImageRotation;
using ::goldfish::memory::SharedMemory;
using ::goldfish::sensors::AndroidSensor;
using ::goldfish::sensors::FoldablePostures;
using ::goldfish::sensors::PhysicalModel;
using ::goldfish::sensors::PhysicalModelChangeEvent;

using DeviceRotation = ::goldfish::physics::Rotation;
using DeviceSkinRotation = ::goldfish::physics::SkinRotation;
using ProtoRotation = android::emulation::control::Rotation;

DisplayServiceImpl::DisplayServiceImpl(::goldfish::display::IMultiDisplay* display,
                                       ::goldfish::sensors::PhysicalModel* pm,
                                       std::vector<::goldfish::parsing::ResizableDisplayConfig> rdc)
        : mMultiDisplay(*display), mPhysicalModel(*pm), resizable_configs_(std::move(rdc)) {
    mRotationSubscription = mMultiDisplay.RegisterPhysicalModel(mPhysicalModel);

    if (!resizable_configs_.empty()) {
        auto screen = mMultiDisplay.GetDisplay(0);
        if (screen.ok()) {
            if (auto d0 = screen->lock()) {
                auto dims = d0->GetDimensions();
                const uint32_t total_modes = static_cast<uint32_t>(resizable_configs_.size());

                for (const auto& rc : resizable_configs_) {
                    if (rc.width == dims.width && rc.height == dims.height) {
                        uint32_t guest_mode_id = total_modes - 1 - rc.id;
                        mMultiDisplay.SetDisplayMode(rc.id, rc.width, rc.height, rc.dpi,
                                                     guest_mode_id);
                        break;
                    }
                }
            }
        }
    }

    if (const auto posture_listener = mPhysicalModel.GetPostureListener()) {
        // Subscribe to future posture changes if the device supports foldables/postures.
        // When posture updates, update display folded state, fire display configuration
        // notifications, and stream posture events over gRPC.
        mPostureSubscription =
                MakeScopedCallback(*posture_listener, [this](const FoldablePostures& posture) {
                    bool isClosed = (posture == FoldablePostures::kClosed);
                    mMultiDisplay.SetFolded(isClosed);

                    fireDisplayConfigurationsChanged();

                    Notification event;
                    event.mutable_posture()->set_value(ToProtoPosture(posture));
                    ::goldfish::avd::GetAvd().GetGrpcNotificationChannel().FireEvent(event);
                });

        // MakeScopedCallback only fires on future updates and does not invoke the callback for
        // the initial state upon registration. Therefore, we explicitly fire the startup posture
        // into the gRPC notification channel during service initialization so that
        // `NotificationStore` captures and caches it. When gRPC clients (like Android Studio's
        // embedded emulator) connect to `streamNotification`, `NotificationStreamWriter`
        // immediately sends this cached initial posture, preventing `currentPosture` from staying
        // null.
        const auto initial_posture = mPhysicalModel.GetFoldablePosture();
        if (initial_posture != FoldablePostures::kUnknown) {
            Notification event;
            event.mutable_posture()->set_value(ToProtoPosture(initial_posture));
            ::goldfish::avd::GetAvd().GetGrpcNotificationChannel().FireEvent(event);
        }
    }
}

Posture::PostureValue DisplayServiceImpl::ToProtoPosture(FoldablePostures posture) {
    switch (posture) {
    case FoldablePostures::kClosed:
        return Posture::POSTURE_CLOSED;
    case FoldablePostures::kHalfOpened:
        return Posture::POSTURE_HALF_OPENED;
    case FoldablePostures::kOpened:
        return Posture::POSTURE_OPENED;
    case FoldablePostures::kFlipped:
        return Posture::POSTURE_FLIPPED;
    case FoldablePostures::kTent:
        return Posture::POSTURE_TENT;
    case FoldablePostures::kPostureMax:
        return Posture::POSTURE_MAX;
    default:
        return Posture::POSTURE_UNKNOWN;
    }
}

PixelFormat PixelFormatFromProtobuf(const ImageFormat_ImgFormat format) {
    switch (format) {
    case ImageFormat::RGB888:
        return PixelFormat::kRgb888;
    case ImageFormat::RGBA8888:
        return PixelFormat::kRgba8888;
    case ImageFormat::PNG:
        return PixelFormat::kPng;
    default:
        return PixelFormat::kRgb888;
    }
}

ProtoRotation toProtobufRotation(const DeviceRotation& rotation) {
    ProtoRotation protoRotation;
    switch (rotation.rotation) {
    case DeviceSkinRotation::kPortrait:
        protoRotation.set_rotation(ProtoRotation::PORTRAIT);
        break;
    case DeviceSkinRotation::kLandscape:
        protoRotation.set_rotation(ProtoRotation::LANDSCAPE);
        break;
    case DeviceSkinRotation::kReversePortrait:
        protoRotation.set_rotation(ProtoRotation::REVERSE_PORTRAIT);
        break;
    case DeviceSkinRotation::kReverseLandscape:
        protoRotation.set_rotation(ProtoRotation::REVERSE_LANDSCAPE);
        break;
    }

    protoRotation.set_xaxis(static_cast<double>(rotation.x_axis));
    protoRotation.set_yaxis(static_cast<double>(rotation.y_axis));
    protoRotation.set_zaxis(static_cast<double>(rotation.z_axis));

    return protoRotation;
}

static uint8_t* standard_allocator(Image* img, size_t size) {
    if (img->image().size() != size) {
        // Proto will release the existing object.
        img->set_allocated_image(new std::string(size, 0));
    }
    auto* raw_data = img->mutable_image()->data();
    return reinterpret_cast<uint8_t*>(raw_data);
}

// Scales `size` down, keeping its aspect ratio, until it fits in `bound`.
static ::goldfish::display::Dimensions FitWithin(::goldfish::display::Dimensions size,
                                                 ::goldfish::display::Dimensions bound) {
    if (size.width <= bound.width && size.height <= bound.height) {
        return size;
    }
    // Both results are at most `bound`, so they fit in uint32_t.
    if (uint64_t{size.width} * bound.height >= uint64_t{size.height} * bound.width) {
        const auto height = static_cast<uint32_t>(uint64_t{size.height} * bound.width / size.width);
        return {.width = bound.width, .height = std::max<uint32_t>(1, height)};
    }
    const auto width = static_cast<uint32_t>(uint64_t{size.width} * bound.height / size.height);
    return {.width = std::max<uint32_t>(1, width), .height = bound.height};
}

Status DisplayServiceImpl::streamScreenshot(ServerContext* context, const ImageFormat* request,
                                            grpc::ServerWriter<Image>* writer) {
    // Make sure we always write the first frame, this can be
    // a completely empty frame if the screen is not active.
    Image reply;
    absl::Hash<std::string> hasher;
    ::goldfish::FpsCalculator fps_calculator(10);

    bool client_available = !context->IsCancelled();

    bool last_frame_was_empty = reply.format().width() == 0;

    ::goldfish::display::SharedDisplay display;
    const bool hw_sensor_hinge = ::goldfish::avd::GetAvd().Props().hw_config.hw_sensor_hinge;
    while (!context->IsCancelled()) {
        auto res = mMultiDisplay.GetActiveDisplay(request->display(), hw_sensor_hinge);
        if (res.ok()) {
            display = *res;
            break;
        }
        VLOG(1) << "streamScreenshot: Waiting for active display " << request->display() << ": "
                << res.status();
        absl::SleepFor(absl::Milliseconds(100));
    }

    if (context->IsCancelled()) {
        LOG(INFO) << "streamScreenshot: request cancelled while waiting for display";
        return Status::CANCELLED;
    }

    auto allocator = createAllocator(*request, *display);
    if (!allocator.ok()) {
        return AbslStatusToGrpcStatus(allocator.status());
    }

    absl::Mutex wait_mu;
    ::goldfish::display::FrameId latest_frame_id ABSL_GUARDED_BY(wait_mu) = 0;
    ::goldfish::display::FrameId seen_frame_id ABSL_GUARDED_BY(wait_mu) = 0;
    ImageRotation latest_rotation ABSL_GUARDED_BY(wait_mu) = display->GetRotation();
    uint64_t aux_event_seq ABSL_GUARDED_BY(wait_mu) = 0;
    uint64_t seen_aux_seq ABSL_GUARDED_BY(wait_mu) = 0;

    auto bump_aux_seq = [&wait_mu, &aux_event_seq]() {
        const absl::MutexLock lock(&wait_mu);
        ++aux_event_seq;
    };

    auto listener = display->AddFrameListener(
            [&wait_mu, &latest_frame_id, &latest_rotation, &aux_event_seq](const FrameEvent& ev) {
                const absl::MutexLock lock(&wait_mu);
                latest_frame_id = std::max(latest_frame_id, ev.frame_id);
                if (ev.rotation != latest_rotation) {
                    latest_rotation = ev.rotation;
                    ++aux_event_seq;
                }
            });

    std::shared_ptr<IDisplay> d0, d1;
    IDisplay::ScopedFrameListener listener0, listener1;
    if (hw_sensor_hinge) {
        auto s0 = mMultiDisplay.GetDisplay(0);
        auto s1 = mMultiDisplay.GetDisplay(1);
        if (s0.ok()) {
            d0 = s0->lock();
            if (d0 && d0.get() != display.get()) {
                listener0 = d0->AddFrameListener(
                        [&bump_aux_seq](const FrameEvent&) { bump_aux_seq(); });
            }
        }
        if (s1.ok()) {
            d1 = s1->lock();
            if (d1 && d1.get() != display.get()) {
                listener1 = d1->AddFrameListener(
                        [&bump_aux_seq](const FrameEvent&) { bump_aux_seq(); });
            }
        }
    }

    bool first_time = true;
    bool retry = false;  // The last read failed in a way the next frame fixes.
    uint64_t last_seq = 0;
    uint64_t total_dropped = 0;
    absl::Time last_frame_time = absl::Now();
    while (client_available) {
        constexpr auto kTimeToWaitForFrame = absl::Milliseconds(125);
        const bool retrying = std::exchange(retry, false);
        bool frames_arrived = first_time;
        if (!first_time || retrying) {
            const absl::MutexLock lock(&wait_mu);
            auto cond = [&]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(wait_mu) {
                return latest_frame_id > seen_frame_id || aux_event_seq > seen_aux_seq;
            };
            frames_arrived = wait_mu.AwaitWithTimeout(absl::Condition(&cond), kTimeToWaitForFrame);
            seen_frame_id = std::max(seen_frame_id, latest_frame_id);
            seen_aux_seq = aux_event_seq;
        } else {
            const absl::MutexLock lock(&wait_mu);
            seen_aux_seq = aux_event_seq;
        }

        if (display->AcquireLatestFrame().removed) {
            return Status(grpc::StatusCode::NOT_FOUND, "Display was removed.");
        }

        if ((frames_arrived || first_time || retrying) && !context->IsCancelled()) {
            auto status = getScreenshot(context, request, &reply, *allocator);
            // UNAVAILABLE: no frame yet, or a device reset or snapshot load
            // fenced the frame; a new one replaces it.
            if (status.error_code() == grpc::StatusCode::FAILED_PRECONDITION ||
                status.error_code() == grpc::StatusCode::UNAVAILABLE) {
                retry = true;
                continue;
            }
            if (!status.ok()) {
                return status;
            }

            {
                const absl::MutexLock lock(&wait_mu);
                seen_frame_id = std::max(seen_frame_id,
                                         static_cast<::goldfish::display::FrameId>(reply.seq()));
            }
            first_time = false;
            // The size of the image might change due to rotation or scaling.

            // We send the first empty frame, after that we wait for
            // frames to come, or until the client gives up on us. So
            // for a screen that comes in and out the client will see
            // this timeline: (0 is empty frame. F is frame) [0, ...
            // <nothing> ..., F1, F2, F3, 0, ...<nothing>... ]
            bool empty_frame = reply.format().width() == 0;
            const uint64_t current_seq = reply.seq();
            if (TRACE_EVENT_CATEGORY_ENABLED("display")) {
                if (last_seq != 0 && current_seq > last_seq + 1) {
                    const uint64_t dropped = current_seq - last_seq - 1;
                    total_dropped += dropped;
                    TRACE_COUNTER("display", "dropped_frames", total_dropped);
                    TRACE_EVENT_INSTANT("display", "FrameDropped", "dropped_count", dropped,
                                        "last_seq", last_seq, "current_seq", current_seq);
                }
                last_seq = current_seq;
            }

            if (!context->IsCancelled() && (!last_frame_was_empty || !empty_frame)) {
                if (TRACE_EVENT_CATEGORY_ENABLED("display")) {
                    const absl::Time now_time = absl::Now();
                    const int64_t interval_ms =
                            absl::ToInt64Milliseconds(now_time - last_frame_time);
                    TRACE_COUNTER("display", "grpc_frame_interval_ms", interval_ms);
                    last_frame_time = now_time;
                }

                VLOG(2) << "Writing frame: " << reply.seq() << ", hash: " << hasher(reply.image());
                {
                    TRACE_EVENT("display", "grpc::ServerWriter::Write", "seq", current_seq, "bytes",
                                reply.ByteSizeLong());
                    client_available = writer->Write(reply);
                }
                if (!client_available) {
                    break;
                }

                // Log the FPS when verbose logging is enabled.
                if (ABSL_VLOG_IS_ON(1)) {
                    fps_calculator.AddFrame();
                    VLOG_EVERY_N_SEC(1, 1)
                            << "gRPC framerate: " << fps_calculator.GetFps() << " fps";
                }
            }
            last_frame_was_empty = empty_frame;
        }
        if (context->IsCancelled()) {
            break;
        }
        client_available = !context->IsCancelled() && client_available;
        if (!client_available) {
            break;
        }
    }
    return Status::OK;
}

Status DisplayServiceImpl::getScreenshot(ServerContext* context, const ImageFormat* request,
                                         Image* reply) {
    const bool hw_sensor_hinge = ::goldfish::avd::GetAvd().Props().hw_config.hw_sensor_hinge;
    auto res = mMultiDisplay.GetActiveDisplay(request->display(), hw_sensor_hinge);
    if (!res.ok()) {
        return AbslStatusToGrpcStatus(res.status());
    }
    auto display = *res;

    auto allocator = createAllocator(*request, *display);
    if (!allocator.ok()) {
        return AbslStatusToGrpcStatus(allocator.status());
    }
    return getScreenshot(context, request, reply, *allocator);
}

absl::StatusOr<DisplayServiceImpl::MemoryAllocator> DisplayServiceImpl::createAllocator(
        const ImageFormat& request, const IDisplay& display) {
    if (!request.has_transport() || request.transport().channel() != ImageTransport::MMAP) {
        return standard_allocator;
    }

    // Reserve the upper bound of pixels we could ever need.
    PixelFormat format = PixelFormatFromProtobuf(request.format());
    size_t bpp = (format == PixelFormat::kRgb888) ? 3 : 4;
    // Pixman requires the stride (in bytes) to be a multiple of 4 bytes.
    auto dims = display.GetDimensions();
    size_t stride = (dims.width * bpp + 3) & ~3;
    size_t max_size = dims.height * stride;
    VLOG(2) << "Requested transport: " << request.transport().ShortDebugString()
            << ", display: " << dims.width << "x" << dims.height << ", max size: " << max_size;
    auto mmap = std::make_unique<SharedMemory>(request.transport().handle(), max_size);
    if (auto status = mmap->Open(SharedMemory::AccessMode::kReadWrite); !status.ok()) {
        return status;
    }
    // Explicitly check if the opened shared memory is smaller than expected.
    if (mmap->Size() < max_size) {
        return absl::OutOfRangeError(
                absl::StrFormat("Shared memory handle %s is too small. Expected at least %d bytes, "
                                "but has %d bytes.",
                                request.transport().handle(), max_size, mmap->Size()));
    }
    return [shm = std::move(mmap)](Image* /* img */, size_t size) -> absl::StatusOr<uint8_t*> {
        if (size > shm->Size()) {
            return absl::OutOfRangeError(
                    absl::StrFormat("Requesting to allocate %d pixels for handle %s that only "
                                    "supports %d pixels",
                                    size, shm->BackingFile().string(), shm->Size()));
        }
        return static_cast<uint8_t*>(shm->Get());
    };
}

Status DisplayServiceImpl::getScreenshot(ServerContext* /*context*/, const ImageFormat* request,
                                         Image* reply, MemoryAllocator& allocator) {
    TRACE_EVENT("display", "DisplayServiceImpl::getScreenshot", "display", request->display());
    bool needs_side_channel =
            request->has_transport() && request->transport().channel() == ImageTransport::MMAP;
    const auto& hw = ::goldfish::avd::GetAvd().Props().hw_config;
    auto res = mMultiDisplay.GetActiveDisplay(request->display(), hw.hw_sensor_hinge);
    if (!res.ok()) {
        return AbslStatusToGrpcStatus(res.status());
    }
    auto display = *res;

    const DeviceRotation device_rotation = mPhysicalModel.GetDeviceRotation();

    FramePtr frame;
    {
        TRACE_EVENT("display", "WaitForFrameArrival");
        auto fresh = display->WaitForFreshFrame(absl::Now() + absl::Milliseconds(500));
        if (fresh.ok()) {
            frame = std::move(*fresh);
        } else if (absl::IsDeadlineExceeded(fresh.status())) {
            auto latest = display->AcquireLatestFrame();
            if (latest.removed) {
                return Status(grpc::StatusCode::NOT_FOUND, "Display was removed.");
            }
            frame = std::move(latest.frame);
        } else {
            return AbslStatusToGrpcStatus(fresh.status());
        }
    }

    if (!frame) {
        return Status(grpc::StatusCode::UNAVAILABLE, "No frame has been produced yet.");
    }

    const ImageRotation rotation = frame->rotation();
    const PixelFormat format = PixelFormatFromProtobuf(request->format());
    ::goldfish::display::Dimensions output{0, 0};
    if (request->width() > 0 && request->height() > 0) {
        const auto dims = frame->dimensions();
        if (dims.width > 0 && dims.height > 0) {
            const bool quarter_turn = rotation == ImageRotation::kRotation90 ||
                                      rotation == ImageRotation::kRotation270;
            int desired_width = request->width();
            const double ratio =
                    static_cast<double>(desired_width) / static_cast<double>(dims.width);
            int desired_height = static_cast<int>(ratio * dims.height);
            if (quarter_turn) {
                std::swap(desired_width, desired_height);
            }
            auto [newWidth, newHeight] =
                    display->ResizeKeepAspectRatio(desired_width, desired_height);
            if (newWidth > 0 && newHeight > 0) {
                // The display fits its current size, which can exceed this
                // frame's (a resize before the next frame). Reads never upscale.
                output = FitWithin(
                        {.width = static_cast<uint32_t>(newWidth),
                         .height = static_cast<uint32_t>(newHeight)},
                        quarter_turn ? ::goldfish::display::Dimensions{dims.height, dims.width}
                                     : dims);
            }
        }
    }
    const ReadRequest req{.rotation = rotation, .output = output};
    ::goldfish::display::Dimensions out_dims{0, 0};

    if (format == PixelFormat::kPng) {
        std::vector<uint8_t> rgba;
        absl::StatusOr<::goldfish::display::Dimensions> dims;
        {
            TRACE_EVENT("display", "display->GetPixels");
            dims = frame->ReadInto(
                    req, [&](const ReadLayout& layout) -> absl::StatusOr<Rgba8888Target> {
                        rgba.resize(layout.total_bytes);
                        return Rgba8888Target{.plane = Plane{.data = absl::MakeSpan(rgba)}};
                    });
        }
        if (!dims.ok()) {
            return AbslStatusToGrpcStatus(dims.status());
        }
        out_dims = *dims;
        std::vector<uint8_t> png_bytes;
        if (!::goldfish::display::write_png(4, out_dims.width, out_dims.height, rgba.data(),
                                            png_bytes)) {
            return Status(grpc::StatusCode::INTERNAL, "Failed to create PNG screenshot!");
        }
        auto allocated_pixels = allocator(reply, png_bytes.size());
        if (!allocated_pixels.ok()) {
            return AbslStatusToGrpcStatus(allocated_pixels.status());
        }
        std::memcpy(*allocated_pixels, png_bytes.data(), png_bytes.size());
        if (!needs_side_channel && png_bytes.size() < reply->mutable_image()->size()) {
            reply->mutable_image()->resize(png_bytes.size());
        }
    } else {
        const ReadFormat read_fmt =
                (format == PixelFormat::kRgb888) ? ReadFormat::kRgb888 : ReadFormat::kRgba8888;
        absl::StatusOr<::goldfish::display::Dimensions> dims;
        {
            TRACE_EVENT("display", "display->GetPixels");
            dims = frame->ReadInto(req, read_fmt, [&](const ReadLayout& layout) {
                return allocator(reply, layout.total_bytes);
            });
        }
        if (!dims.ok()) {
            return AbslStatusToGrpcStatus(dims.status());
        }
        out_dims = *dims;
    }

    if (needs_side_channel) {
        auto* transport = reply->mutable_format()->mutable_transport();
        transport->set_handle(request->transport().handle());
        transport->set_channel(ImageTransport::MMAP);
    }

    auto out_format = reply->mutable_format();
    out_format->set_width(static_cast<int>(out_dims.width));
    out_format->set_height(static_cast<int>(out_dims.height));
    out_format->set_format(request->format());
    out_format->set_display(display->Id());
    *out_format->mutable_rotation() = toProtobufRotation(device_rotation);

    const FrameEvent& ev = frame->event();
    const absl::Time ts = (ev.capture_time == absl::InfinitePast()) ? absl::Now() : ev.capture_time;
    reply->set_timestampus(absl::ToUnixMicros(ts));
    reply->set_seq(static_cast<uint32_t>(ev.frame_id));

    if (!hw.hw_resizable_configs.empty()) {
        reply->mutable_format()->set_displaymode(
                static_cast<DisplayModeValue>(mMultiDisplay.GetDisplayMode()));
    }

    if (hw.hw_sensor_hinge) {
        if (mMultiDisplay.IsFolded()) {
            int fx, fy, fw, fh;
            if (mPhysicalModel.GetFoldedArea(&fx, &fy, &fw, &fh)) {
                auto folded_display = out_format->mutable_foldeddisplay();
                folded_display->set_width(fw);
                folded_display->set_height(fh);
                folded_display->set_xoffset(fx);
                folded_display->set_yoffset(fy);
            }
        } else {
            reply->mutable_format()->clear_foldeddisplay();
        }
    }

    VLOG(2) << "Produced frame: " << out_format->ShortDebugString();
    return Status::OK;
}

Status DisplayServiceImpl::getDisplayConfigurations(const IMultiDisplay& multiDisplay,
                                                    DisplayConfigurations* reply) {
    for (uint32_t i = 0; i < multiDisplay.kMaxDisplays; ++i) {
        auto screen = multiDisplay.GetDisplay(i);
        if (screen.ok()) {
            if (auto display = screen->lock()) {
                if (!multiDisplay.IsActive(i)) continue;
                auto cfg = reply->add_displays();
                auto dims = display->GetDimensions();
                cfg->set_width(dims.width);
                cfg->set_height(dims.height);
                cfg->set_dpi(display->Dpi());
                cfg->set_display(display->Id());
                cfg->set_flags(display->Flags());
            }
        }
    }

    // TODO(jansene): Where should these really come from?
    reply->set_maxdisplays(multiDisplay.kMaxDisplays);
    reply->set_userconfigurable(3);

    return Status::OK;
}

Status DisplayServiceImpl::setDisplayConfigurations(ServerContext* context,
                                                    const DisplayConfigurations* request,
                                                    DisplayConfigurations* reply) {
    std::unordered_set<uint32_t> requested_ids;
    // Validation
    for (int i = 0; i < request->displays_size(); ++i) {
        const auto& disp = request->displays(i);
        uint32_t id = disp.display();

        if (id == 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "Display ID 0 (primary) cannot be modified.");
        }
        if (disp.width() == 0 || disp.height() == 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "Display width and height must be greater than 0.");
        }
        if (!requested_ids.insert(id).second) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "Duplicate display ID found in request.");
        }
    }

    // Fetch current state to compute the diffs
    DisplayConfigurations current_config;
    Status status = getDisplayConfigurations(context, nullptr, &current_config);
    if (!status.ok()) {
        return status;
    }

    std::unordered_set<uint32_t> current_ids;
    for (int i = 0; i < current_config.displays_size(); ++i) {
        uint32_t id = current_config.displays(i).display();
        if (id != 0) {
            current_ids.insert(id);
        }
    }

    // Apply changes: DEL missing displays
    for (uint32_t current_id : current_ids) {
        if (requested_ids.find(current_id) == requested_ids.end()) {
            // Display is in current state but missing from request -> delete it
            mMultiDisplay.EraseDisplay(current_id).IgnoreError();
        }
    }

    // Apply changes: ADD new or UPDATE changed displays
    for (int i = 0; i < request->displays_size(); ++i) {
        const auto& disp = request->displays(i);
        uint32_t id = disp.display();

        bool need_create = false;
        if (current_ids.find(id) == current_ids.end()) {
            need_create = true;
        } else {
            // Display ID already exists -> check if it has changed
            auto screen = mMultiDisplay.GetDisplay(id);
            if (screen.ok()) {
                if (auto display = screen->lock()) {
                    auto dims = display->GetDimensions();
                    if (dims.width != disp.width() || dims.height != disp.height() ||
                        display->Dpi() != disp.dpi() || display->Flags() != disp.flags()) {
                        // Configuration changed -> erase the display first so it can be recreated
                        mMultiDisplay.EraseDisplay(id).IgnoreError();
                        need_create = true;
                    }
                }
            }
        }

        if (need_create) {
            mMultiDisplay.CreateDisplay(id, disp.width(), disp.height(), disp.dpi(), disp.flags())
                    .IgnoreError();
        }
    }

    // Populate the final reply
    // We call getDisplayConfigurations again to get the verified updated state
    // to send back to the client.
    getDisplayConfigurations(context, nullptr, reply);
    fireDisplayConfigurationsChanged();

    return Status::OK;
}

Status DisplayServiceImpl::getDisplayMode(ServerContext* context, const Empty* request,
                                          DisplayMode* reply) {
    if (resizable_configs_.empty()) {
        return Status(grpc::StatusCode::FAILED_PRECONDITION, "AVD is not resizable.");
    }

    reply->set_value(static_cast<DisplayModeValue>(mMultiDisplay.GetDisplayMode()));
    return Status::OK;
}

Status DisplayServiceImpl::setDisplayMode(ServerContext* context, const DisplayMode* request,
                                          Empty* reply) {
    if (resizable_configs_.empty()) {
        return Status(grpc::StatusCode::FAILED_PRECONDITION, "AVD is not resizable.");
    }

    bool found = false;
    uint32_t target_w = 0, target_h = 0;
    uint32_t target_dpi = 0;

    for (const auto& rc : resizable_configs_) {
        if (rc.id == static_cast<uint32_t>(request->value())) {
            target_w = rc.width;
            target_h = rc.height;
            target_dpi = rc.dpi;
            found = true;
            break;
        }
    }

    if (!found) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT, "Invalid display mode.");
    }

    if (request->value() == FOLDABLE) {
        const auto posture = mPhysicalModel.GetFoldablePosture();

        Notification event;
        event.mutable_posture()->set_value(ToProtoPosture(posture));
        ::goldfish::avd::GetAvd().GetGrpcNotificationChannel().FireEvent(event);
    }

    uint32_t total_modes = static_cast<uint32_t>(resizable_configs_.size());
    uint32_t requested_mode_id = static_cast<uint32_t>(request->value());
    uint32_t guest_mode_id = total_modes - 1 - requested_mode_id;

    mMultiDisplay.SetDisplayMode(request->value(), target_w, target_h, target_dpi, guest_mode_id);
    fireDisplayConfigurationsChanged();

    return Status::OK;
}

void DisplayServiceImpl::fireDisplayConfigurationsChanged() {
    // Fill and fire the notification event
    auto& avd_universe = ::goldfish::avd::GetAvd();
    ::android::emulation::control::Notification event;

    // Set the oneof type to DisplayConfigurationsChangedNotification
    auto* changed_notification = event.mutable_displayconfigurationschangednotification();

    // Copy the populated reply into the notification payload
    getDisplayConfigurations(mMultiDisplay, changed_notification->mutable_displayconfigurations());

    // Fire the event to any connected gRPC notification streams
    avd_universe.GetGrpcNotificationChannel().FireEvent(event);
}

Status DisplayServiceImpl::getDisplayConfigurations(ServerContext* context, const Empty* request,
                                                    DisplayConfigurations* reply) {
    return getDisplayConfigurations(mMultiDisplay, reply);
}

}  // namespace control
}  // namespace emulation
}  // namespace android
