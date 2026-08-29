
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

#include <cstdint>
#include <memory>

#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/time/time.h"
#include "grpcpp/grpcpp.h"

#include "android/base/system.h"
#include "android/emulation/control/absl_status_translate.h"
#include "android/goldfish/hardware_config.h"
#include "goldfish/avd_info/avd_info.h"
#include "goldfish/devices/multidisplay/multidisplay_device.h"
#include "goldfish/display/multi_display_callbacks.h"
#include "goldfish/display/pixman_display.h"
#include "goldfish/eventing/multi_event_source_waiter.h"
#include "goldfish/eventing/observable_value.h"
#include "goldfish/fps_calculator.h"
#include "goldfish/memory/shared_memory.h"
#include "goldfish/physics/rotation.h"

namespace android {
namespace emulation {
namespace control {

using android::base::eventing::MultiEventSourceWaiter;
using ::goldfish::display::FrameInfo;
using ::goldfish::display::FrameInfoCallbackSource;
using ::goldfish::display::IDisplay;
using ::goldfish::display::ImageRotation;
using ::goldfish::display::IMultiDisplay;
using ::goldfish::display::PixelFormat;
using ::goldfish::eventing::ObservableValue;
using ::goldfish::eventing::ObservableValueTriggerOnUpdate;
using ::goldfish::memory::SharedMemory;
using ::goldfish::sensors::AndroidSensor;
using ::goldfish::sensors::FoldablePostures;
using ::goldfish::sensors::PhysicalModel;
using ::goldfish::sensors::PhysicalModelChangeEvent;

using DeviceRotation = ::goldfish::physics::Rotation;
using DeviceSkinRotation = ::goldfish::physics::SkinRotation;
using ProtoRotation = android::emulation::control::Rotation;

using DeviceSkinRotationCallbackSource =
        ObservableValue<DeviceSkinRotation, ObservableValueTriggerOnUpdate>;

DisplayServiceImpl::DisplayServiceImpl(::goldfish::display::IMultiDisplay* display,
                                       ::goldfish::sensors::PhysicalModel* pm,
                                       std::vector<::goldfish::parsing::ResizableDisplayConfig> rdc)
        : mMultiDisplay(*display), mPhysicalModel(*pm), resizable_configs_(std::move(rdc)) {
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

    if (mPhysicalModel.HasFoldableModel()) {
        // Subscribe to future posture changes if the device supports foldables/postures.
        // When posture updates, update display folded state, fire display configuration
        // notifications, and stream posture events over gRPC.
        mPostureSubscription = MakeScopedCallback(
                mPhysicalModel.GetPostureListener(), [this](const FoldablePostures& posture) {
                    bool isClosed = (posture == FoldablePostures::kClosed);
                    mMultiDisplay.SetFolded(isClosed);

                    fireDisplayConfigurationsChanged();

                    Notification event;
                    event.mutable_posture()->set_value(ToProtoPosture(posture));
                    ::goldfish::avd_info::GetAvd().GetGrpcNotificationChannel().FireEvent(event);
                });

        // MakeScopedCallback only fires on future updates and does not invoke the callback for
        // the initial state upon registration. Therefore, we explicitly fire the startup posture
        // into the gRPC notification channel during service initialization so that
        // `NotificationStore` captures and caches it. When gRPC clients (like Android Studio's
        // embedded emulator) connect to `streamNotification`, `NotificationStreamWriter`
        // immediately sends this cached initial posture, preventing `currentPosture` from staying
        // null.
        const auto initial_posture = mPhysicalModel.GetFoldableState().current_posture;
        if (initial_posture != FoldablePostures::kUnknown) {
            Notification event;
            event.mutable_posture()->set_value(ToProtoPosture(initial_posture));
            ::goldfish::avd_info::GetAvd().GetGrpcNotificationChannel().FireEvent(event);
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

Status DisplayServiceImpl::streamScreenshot(ServerContext* context, const ImageFormat* request,
                                            grpc::ServerWriter<Image>* writer) {
    // Make sure we always write the first frame, this can be
    // a completely empty frame if the screen is not active.
    Image reply;
    absl::Hash<std::string> hasher;
    ::goldfish::FpsCalculator fpsCalculator(10);

    bool clientAvailable = !context->IsCancelled();

    bool lastFrameWasEmpty = reply.format().width() == 0;
    int frame = 0;

    ::goldfish::display::SharedDisplay display;
    const bool hw_sensor_hinge = ::goldfish::avd_info::GetAvd().Props().hw_config.hw_sensor_hinge;
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

    DeviceSkinRotationCallbackSource deviceSkinRotationCallbackSource;
    const auto deviceSkinRotationSubscription = android::base::eventing::MakeScopedCallback(
            mPhysicalModel,
            [this, &deviceSkinRotationCallbackSource](const PhysicalModelChangeEvent& event) {
                if (event.type == PhysicalModelChangeEvent::Type::kTargetStateChanged) {
                    deviceSkinRotationCallbackSource.FireEvent(
                            mPhysicalModel.GetDeviceRotation().rotation);
                }
            });

    MultiEventSourceWaiter frameOrSensorEvent;
    frameOrSensorEvent.Listen<FrameInfoCallbackSource>(display.get());

    std::shared_ptr<IDisplay> d0, d1;
    if (hw_sensor_hinge) {
        auto s0 = mMultiDisplay.GetDisplay(0);
        auto s1 = mMultiDisplay.GetDisplay(1);
        if (s0.ok()) {
            d0 = s0->lock();
            if (d0 && d0.get() != display.get()) {
                frameOrSensorEvent.Listen<FrameInfoCallbackSource>(d0.get());
            }
        }
        if (s1.ok()) {
            d1 = s1->lock();
            if (d1 && d1.get() != display.get()) {
                frameOrSensorEvent.Listen<FrameInfoCallbackSource>(d1.get());
            }
        }
    }
    frameOrSensorEvent.Listen<DeviceSkinRotationCallbackSource>(&deviceSkinRotationCallbackSource);

    // TODO(jansene): Bring back metrics.
    // Track percentiles, and report if we have seen at least 32 frames.
    // metrics::Percentiles perfEstimator(32, {0.5, 0.95});
    bool firstTime = true;
    while (clientAvailable) {
        const auto kTimeToWaitForFrame = absl::Milliseconds(125);
        bool framesArrived = frameOrSensorEvent.WaitForNextEvent(kTimeToWaitForFrame, frame);
        if ((framesArrived || firstTime) && !context->IsCancelled()) {
            // TODO(jansene): It might have been possible for a frame to have been
            // delivered between framesArrived and this call, which resulted in
            // the increment of the frame counter. We would not "see" this frame.
            frame = frameOrSensorEvent.GetEventSequence();
            auto status = getScreenshot(context, request, &reply, *allocator);
            if (status.error_code() == grpc::StatusCode::FAILED_PRECONDITION) {
                continue;
            }
            if (!status.ok()) {
                return status;
            }

            firstTime = false;
            // The size of the image might change due to rotation or scaling.

            // We send the first empty frame, after that we wait for
            // frames to come, or until the client gives up on us. So
            // for a screen that comes in and out the client will see
            // this timeline: (0 is empty frame. F is frame) [0, ...
            // <nothing> ..., F1, F2, F3, 0, ...<nothing>... ]
            bool emptyFrame = reply.format().width() == 0;
            if (!context->IsCancelled() && (!lastFrameWasEmpty || !emptyFrame)) {
                VLOG(2) << "Writing frame: " << reply.seq() << ", hash: " << hasher(reply.image());
                clientAvailable = writer->Write(reply);
                if (!clientAvailable) {
                    break;
                }

                // Log the FPS when verbose logging is enabled.
                if (ABSL_VLOG_IS_ON(1)) {
                    fpsCalculator.AddFrame();
                    VLOG_EVERY_N_SEC(1, 1)
                            << "gRPC framerate: " << fpsCalculator.GetFps() << " fps";
                }
            }
            lastFrameWasEmpty = emptyFrame;
        }
        if (context->IsCancelled()) {
            break;
        }
        clientAvailable = !context->IsCancelled() && clientAvailable;
        if (!clientAvailable) {
            break;
        }
    }
    return Status::OK;
}

Status DisplayServiceImpl::getScreenshot(ServerContext* context, const ImageFormat* request,
                                         Image* reply) {
    const bool hw_sensor_hinge = ::goldfish::avd_info::GetAvd().Props().hw_config.hw_sensor_hinge;
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

Status DisplayServiceImpl::getScreenshot(ServerContext* context, const ImageFormat* request,
                                         Image* reply, MemoryAllocator& allocator) {
    bool needs_side_channel =
            request->has_transport() && request->transport().channel() == ImageTransport::MMAP;
    const auto& hw = ::goldfish::avd_info::GetAvd().Props().hw_config;
    auto res = mMultiDisplay.GetActiveDisplay(request->display(), hw.hw_sensor_hinge);
    if (!res.ok()) {
        return AbslStatusToGrpcStatus(res.status());
    }
    auto display = *res;

    const DeviceRotation deviceRotation = mPhysicalModel.GetDeviceRotation();
    auto dims = display->GetDimensions();
    int desired_width = request->width();
    int desired_height = request->height();

    // User wants to use device width/height
    if (desired_width == 0 || desired_height == 0) {
        desired_width = dims.width;
        desired_height = dims.height;
    }

    // the desired_width and display height are not stable at the moment
    // they switch from 616x1218 to 616x1080, and that behavior
    // caused some confusion in embedded ui; in addition, the
    // sensor does not give correct orientation neither, sometime
    // it shows landscape, no idea what went wrong. for now,
    // just do a simple scale according to the ratio of display w/h
    // TODO: fix this b/448504524
    const double desired_over_display_ratio = ((double)desired_width) / ((double)dims.width);
    desired_height = (int)(desired_over_display_ratio * dims.height);

    // Depending on the rotation state width and height need to be
    // reversed. as our apsect ration depends on how we are holding our
    // phone..
    if (deviceRotation.rotation == DeviceSkinRotation::kLandscape ||
        deviceRotation.rotation == DeviceSkinRotation::kReverseLandscape) {
        VLOG(2) << "Swapping width & height " << desired_width << "x" << desired_height << " to "
                << desired_height << "x" << desired_width;
        std::swap(desired_width, desired_height);

        // TODO(jansene): Support for folded device.
        // if (not_pixel_fold && isFolded) {
        //     std::swap(rect.pos.x, rect.pos.y);
        //     std::swap(rect.size.w, rect.size.h);
        // }
    }

    // Calculate width and height, keeping aspect ratio in mind.
    auto [newWidth, newHeight] = display->ResizeKeepAspectRatio(desired_width, desired_height);
    VLOG(2) << "Resizing from " << desired_width << "x" << desired_height << " to " << newWidth
            << "x" << newHeight;

    ImageRotation rotation = ImageRotation::kRotation0;
    switch (deviceRotation.rotation) {
    case DeviceSkinRotation::kPortrait:
        rotation = ImageRotation::kRotation0;
        break;
    case DeviceSkinRotation::kLandscape:
        rotation = ImageRotation::kRotation90;
        break;
    case DeviceSkinRotation::kReversePortrait:
        rotation = ImageRotation::kRotation180;
        break;
    case DeviceSkinRotation::kReverseLandscape:
        rotation = ImageRotation::kRotation270;
        break;
    }

    // Let's figure out how many pixels we need, and allocate a buffer than can hold it.
    size_t c_pixels = 0;
    PixelFormat format = PixelFormatFromProtobuf(request->format());

    auto seq = display->GetPixels(format, newWidth, newHeight, rotation, /*pixels=*/nullptr,
                                  &c_pixels);
    DCHECK(absl::IsFailedPrecondition(seq.status()))
            << "The c-style callback should inform us how many bytes we should allocate.";

    auto allocated_pixels = allocator(reply, c_pixels);
    if (!allocated_pixels.ok()) {
        return AbslStatusToGrpcStatus(allocated_pixels.status());
    }
    seq = display->GetPixels(format, newWidth, newHeight, rotation, *allocated_pixels, &c_pixels);

    if (!seq.status().ok()) {
        return AbslStatusToGrpcStatus(seq.status());
    }

    // Make sure studio does not get confused, as the pixels required for png < image size..
    if (!needs_side_channel && format == PixelFormat::kPng &&
        c_pixels < reply->mutable_image()->size()) {
        reply->mutable_image()->resize(c_pixels);
    }

    if (needs_side_channel) {
        auto* transport = reply->mutable_format()->mutable_transport();
        transport->set_handle(request->transport().handle());
        transport->set_channel(ImageTransport::MMAP);
    }

    auto outFormat = reply->mutable_format();
    outFormat->set_width(newWidth);
    outFormat->set_height(newHeight);
    outFormat->set_format(request->format());
    outFormat->set_display(display->Id());
    *outFormat->mutable_rotation() = toProtobufRotation(deviceRotation);

    // TODO(jansene): Do we want android frame timestamp or now?
    reply->set_timestampus(absl::ToUnixMicros(seq->timestamp));
    reply->set_seq(seq->sequence_number);

    if (!hw.hw_resizable_configs.empty()) {
        reply->mutable_format()->set_displaymode(
                static_cast<DisplayModeValue>(mMultiDisplay.GetDisplayMode()));
    }

    if (hw.hw_sensor_hinge) {
        if (mMultiDisplay.IsFolded()) {
            int fx, fy, fw, fh;
            if (mPhysicalModel.GetFoldedArea(&fx, &fy, &fw, &fh)) {
                auto foldedDisplay = outFormat->mutable_foldeddisplay();
                foldedDisplay->set_width(fw);
                foldedDisplay->set_height(fh);
                foldedDisplay->set_xoffset(fx);
                foldedDisplay->set_yoffset(fy);
            }
        } else {
            reply->mutable_format()->clear_foldeddisplay();
        }
    }

    VLOG(2) << "Produced frame: " << outFormat->ShortDebugString();
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
        const auto posture = mPhysicalModel.GetFoldableState().current_posture;

        Notification event;
        event.mutable_posture()->set_value(ToProtoPosture(posture));
        ::goldfish::avd_info::GetAvd().GetGrpcNotificationChannel().FireEvent(event);
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
    auto& avdUniverse = ::goldfish::avd_info::GetAvd();
    ::android::emulation::control::Notification event;

    // Set the oneof type to DisplayConfigurationsChangedNotification
    auto* changed_notification = event.mutable_displayconfigurationschangednotification();

    // Copy the populated reply into the notification payload
    getDisplayConfigurations(mMultiDisplay, changed_notification->mutable_displayconfigurations());

    // Fire the event to any connected gRPC notification streams
    avdUniverse.GetGrpcNotificationChannel().FireEvent(event);
}

Status DisplayServiceImpl::getDisplayConfigurations(ServerContext* context, const Empty* request,
                                                    DisplayConfigurations* reply) {
    return getDisplayConfigurations(mMultiDisplay, reply);
}

}  // namespace control
}  // namespace emulation
}  // namespace android
