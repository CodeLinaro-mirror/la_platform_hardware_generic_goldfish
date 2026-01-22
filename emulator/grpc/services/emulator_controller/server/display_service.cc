
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
#include "emulator/grpc/services/emulator_controller/server/display_service.h"

#include <cstdint>
#include <memory>

#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/time/time.h"
#include "grpcpp/grpcpp.h"

#include "android/base/system.h"
#include "android/emulation/control/absl_status_translate.h"
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
using ::goldfish::sensors::PhysicalModel;
using ::goldfish::sensors::PhysicalModelChangeEvent;

using DeviceRotation = ::goldfish::physics::Rotation;
using DeviceSkinRotation = ::goldfish::physics::SkinRotation;
using ProtoRotation = android::emulation::control::Rotation;

using DeviceSkinRotationCallbackSource =
        ObservableValue<DeviceSkinRotation, ObservableValueTriggerOnUpdate>;

PixelFormat PixelFormatFromProtobuf(const ImageFormat_ImgFormat format) {
    switch (format) {
    case ImageFormat::RGB888:
        return PixelFormat::RGB888;
    case ImageFormat::RGBA8888:
        return PixelFormat::RGBA8888;
    case ImageFormat::PNG:
        return PixelFormat::PNG;
    default:
        return PixelFormat::RGB888;
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

    auto screen = mMultiDisplay.getDisplay(request->display());
    if (!screen.ok()) {
        LOG(INFO) << "Unable to retrieve display: " << screen.status();
        return abslStatusToGrpcStatus(screen.status());
    }
    auto display = screen->lock();
    if (!display) {
        return Status(grpc::StatusCode::UNAVAILABLE, "Display is no longer active.");
    }

    auto allocator = createAllocator(*request, *display);
    if (!allocator.ok()) {
        return abslStatusToGrpcStatus(allocator.status());
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

                // Log the FPS when verbose logging is enabled.
                if (ABSL_VLOG_IS_ON(1)) {
                    fpsCalculator.AddFrame();
                    VLOG_EVERY_N_SEC(1, 1)
                            << "gRPC framerate: " << fpsCalculator.GetFps() << " fps";
                }
            }
            lastFrameWasEmpty = emptyFrame;
        }
        clientAvailable = !context->IsCancelled() && clientAvailable;
    }
    return Status::OK;
}

Status DisplayServiceImpl::getScreenshot(ServerContext* context, const ImageFormat* request,
                                         Image* reply) {
    auto screen = mMultiDisplay.getDisplay(request->display());
    if (!screen.ok()) {
        LOG(INFO) << "Unable to retrieve display: " << screen.status();
        return abslStatusToGrpcStatus(screen.status());
    }
    auto display = screen->lock();
    if (!display) {
        return Status(grpc::StatusCode::UNAVAILABLE, "Display is no longer active.");
    }

    auto allocator = createAllocator(*request, *display);
    if (!allocator.ok()) {
        return abslStatusToGrpcStatus(allocator.status());
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
    size_t bpp = (format == PixelFormat::RGB888) ? 3 : 4;
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
    auto screen = mMultiDisplay.getDisplay(request->display());
    auto display = screen->lock();

    const DeviceRotation deviceRotation = mPhysicalModel.GetDeviceRotation();
    auto dims = display->GetDimensions();
    int desiredWidth = request->width();
    int desiredHeight = request->height();

    // User wants to use device width/height
    if (desiredWidth == 0 || desiredHeight == 0) {
        desiredWidth = dims.width;
        desiredHeight = dims.height;
    }

    // the desiredWidth and display height are not stable at the moment
    // they switch from 616x1218 to 616x1080, and that behavior
    // caused some confusion in embedded ui; in addition, the
    // sensor does not give correct orientation neither, sometime
    // it shows landscape, no idea what went wrong. for now,
    // just do a simple scale according to the ratio of display w/h
    // TODO: fix this b/448504524
    const double desired_over_display_ratio = ((double)desiredWidth) / ((double)dims.width);
    desiredHeight = (int)(desired_over_display_ratio * dims.height);


    // Depending on the rotation state width and height need to be
    // reversed. as our apsect ration depends on how we are holding our
    // phone..
    if (deviceRotation.rotation == DeviceSkinRotation::kLandscape ||
        deviceRotation.rotation == DeviceSkinRotation::kReverseLandscape) {
        VLOG(2) << "Swapping width & height " << desiredWidth << "x" << desiredHeight << " to "
                << desiredHeight << "x" << desiredWidth;
        std::swap(desiredWidth, desiredHeight);

        // TODO(jansene): Support for folded device.
        // if (not_pixel_fold && isFolded) {
        //     std::swap(rect.pos.x, rect.pos.y);
        //     std::swap(rect.size.w, rect.size.h);
        // }
    }

    // Calculate width and height, keeping aspect ratio in mind.
    auto [newWidth, newHeight] = display->resizeKeepAspectRatio(desiredWidth, desiredHeight);
    VLOG(2) << "Resizing from " << desiredWidth << "x" << desiredHeight << " to " << newWidth << "x"
            << newHeight;

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
    size_t cPixels = 0;
    PixelFormat format = PixelFormatFromProtobuf(request->format());

    auto seq =
            display->getPixels(format, newWidth, newHeight, rotation, /*pixels=*/nullptr, &cPixels);
    DCHECK(absl::IsFailedPrecondition(seq.status()))
            << "The c-style callback should inform us how many bytes we should allocate.";

    auto allocated_pixels = allocator(reply, cPixels);
    if (!allocated_pixels.ok()) {
        return abslStatusToGrpcStatus(allocated_pixels.status());
    }
    seq = display->getPixels(format, newWidth, newHeight, rotation, *allocated_pixels, &cPixels);

    if (!seq.status().ok()) {
        return abslStatusToGrpcStatus(seq.status());
    }

    // Make sure studio does not get confused, as the pixels required for png < image size..
    if (!needs_side_channel && format == PixelFormat::PNG &&
        cPixels < reply->mutable_image()->size()) {
        reply->mutable_image()->resize(cPixels);
    }

    if (needs_side_channel) {
        auto* transport = reply->mutable_format()->mutable_transport();
        transport->set_handle(request->transport().handle());
        transport->set_channel(ImageTransport::MMAP);
    }

    auto outFormat = reply->mutable_format();
    outFormat->set_width(newWidth);
    outFormat->set_height(newHeight);
    outFormat->set_display(display->id());
    *outFormat->mutable_rotation() = toProtobufRotation(deviceRotation);

    // TODO(jansene): Do we want android frame timestamp or now?
    reply->set_timestampus(absl::ToUnixMicros(seq->timestamp));
    reply->set_seq(seq->sequenceNumber);

    VLOG(2) << "Produced frame: " << outFormat->ShortDebugString();
    return Status::OK;
}

Status DisplayServiceImpl::getDisplayConfigurations(const IMultiDisplay& multiDisplay,
                                                    DisplayConfigurations* reply) {
    for (const auto& weakdisplay : multiDisplay.displays()) {
        if (auto display = weakdisplay.lock()) {
            auto cfg = reply->add_displays();
            auto dims = display->GetDimensions();
            cfg->set_width(dims.width);
            cfg->set_height(dims.height);
            cfg->set_dpi(display->dpi());
            cfg->set_display(display->id());
            cfg->set_flags(display->flags());
        }
    }

    // TODO(jansene): Where should these really come from?
    reply->set_maxdisplays(multiDisplay.maxDisplays);
    reply->set_userconfigurable(3);

    return Status::OK;
}

Status DisplayServiceImpl::getDisplayConfigurations(ServerContext* context, const Empty* request,
                                                    DisplayConfigurations* reply) {
    return getDisplayConfigurations(mMultiDisplay, reply);
}

}  // namespace control
}  // namespace emulation
}  // namespace android
