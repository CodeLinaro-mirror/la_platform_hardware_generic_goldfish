
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
#include "android/emulation/control/DisplayService.h"

#include <cstdint>
#include <memory>

#include "absl/hash/hash.h"
#include "absl/log/log.h"
#include "absl/time/time.h"
#include "grpcpp/grpcpp.h"

#include "aemu/base/Tracing.h"
#include "aemu/base/events/MultiEventSourceWaiter.h"
#include "android/base/system/System.h"
#include "android/goldfish/display/Display.h"
#include "android/goldfish/display/FpsCalculator.h"
#include "android/goldfish/display/MultiDisplay.h"
#include "android/grpc/utils/AbslStatusTranslate.h"
#include "goldfish/devices/sensor/SensorDevice.h"

namespace android {
namespace emulation {
namespace control {

using android::base::eventing::MultiEventSourceWaiter;
using android::goldfish::FrameInfo;
using android::goldfish::IDisplay;
using android::goldfish::IMultiDisplay;
using android::goldfish::PixelFormat;
using ::goldfish::devices::sensor::AndroidSensor;
using ::goldfish::devices::sensor::ISensorDevice;
using ::goldfish::devices::sensor::SensorData;
using ::goldfish::devices::sensor::SensorObserver;
using ::grpc::Status;
using DeviceRotation = ::goldfish::physics::Rotation;
using DeviceSkinRotation = ::goldfish::physics::SkinRotation;
using ProtoRotation = android::emulation::control::Rotation;

ProtoRotation toProtobufRotation(const DeviceRotation& rotation) {
    ProtoRotation protoRotation;
    switch (rotation.rotation) {
    case DeviceSkinRotation::PORTRAIT:
        protoRotation.set_rotation(ProtoRotation::PORTRAIT);
        break;
    case DeviceSkinRotation::LANDSCAPE:
        protoRotation.set_rotation(ProtoRotation::LANDSCAPE);
        break;
    case DeviceSkinRotation::REVERSE_PORTRAIT:
        protoRotation.set_rotation(ProtoRotation::REVERSE_PORTRAIT);
        break;
    case DeviceSkinRotation::REVERSE_LANDSCAPE:
        protoRotation.set_rotation(ProtoRotation::REVERSE_LANDSCAPE);
        break;
    }

    protoRotation.set_xaxis(static_cast<double>(rotation.xAxis));
    protoRotation.set_yaxis(static_cast<double>(rotation.yAxis));
    protoRotation.set_zaxis(static_cast<double>(rotation.zAxis));

    return protoRotation;
}

Status DisplayServiceImpl::streamScreenshot(ServerContext* context, const ImageFormat* request,
                                            grpc::ServerWriter<Image>* writer) {
    // Make sure we always write the first frame, this can be
    // a completely empty frame if the screen is not active.
    Image reply;
    absl::Hash<std::string> hasher;
    goldfish::FpsCalculator fpsCalculator(10);

    // cPixels is used to verify the invariant that retrieved image
    // is not shrinking over subsequent calls, as this might result
    // in unexpected behavior for clients.
    int cPixels = reply.image().size();
    bool clientAvailable = !context->IsCancelled();

    bool lastFrameWasEmpty = reply.format().width() == 0;
    int frame = 0;

    auto screen = mMultiDisplay->getDisplay(request->display());
    if (!screen.ok()) {
        return Status(::grpc::StatusCode::INVALID_ARGUMENT,
                      "Invalid display: " + std::to_string(request->display()), "");
    }
    auto display = screen->lock();
    if (!display) {
        return Status(::grpc::StatusCode::INVALID_ARGUMENT,
                      "Invalid display: " + std::to_string(request->display()), "");
    }

    SensorObserver accObserver(mRegistry, AndroidSensor::ACCELERATION);
    MultiEventSourceWaiter frameOrSensorEvent;
    frameOrSensorEvent.listen(&accObserver);
    frameOrSensorEvent.listen<goldfish::FrameInfoCallbackSource>(display.get());

    // TODO(jansene): Bring back metrics.
    // Track percentiles, and report if we have seen at least 32 frames.
    // metrics::Percentiles perfEstimator(32, {0.5, 0.95});
    bool firstTime = true;
    while (clientAvailable) {
        const auto kTimeToWaitForFrame = absl::Milliseconds(125);
        bool framesArrived = frameOrSensorEvent.waitForNextEvent(kTimeToWaitForFrame, frame);
        if ((framesArrived || firstTime) && !context->IsCancelled()) {
            AEMU_SCOPED_TRACE("streamScreenshot::frame\r\n");

            // TODO(jansene): It might have been possible for a frame to have been
            // delivered between framesArrived and this call, which resulted in
            // the increment of the frame counter. We would not "see" this frame.
            frame = frameOrSensorEvent.getEventSequence();
            auto status = getScreenshot(context, request, &reply);
            if (status.error_code() == grpc::StatusCode::FAILED_PRECONDITION) {
                continue;
            }

            if (!status.ok()) {
                return status;
            }

            firstTime = false;
            // The invariant that the pixel buffer does not decrease
            // should hold. Clients likely rely on the buffer size to
            // match the actual number of available pixels.
            assert(reply.image().size() >= cPixels);
            cPixels = reply.image().size();

            // We send the first empty frame, after that we wait for
            // frames to come, or until the client gives up on us. So
            // for a screen that comes in and out the client will see
            // this timeline: (0 is empty frame. F is frame) [0, ...
            // <nothing> ..., F1, F2, F3, 0, ...<nothing>... ]
            bool emptyFrame = reply.format().width() == 0;
            if (!context->IsCancelled() && (!lastFrameWasEmpty || !emptyFrame)) {
                AEMU_SCOPED_TRACE("streamScreenshot::write");
                VLOG(2) << "Writing frame: " << reply.seq() << ", hash: " << hasher(reply.image());
                clientAvailable = writer->Write(reply);

                // Log the FPS when verbose logging is enabled.
                if (ABSL_VLOG_IS_ON(1)) {
                    fpsCalculator.addFrame();
                    VLOG_EVERY_N_SEC(1, 1)
                            << "gRPC framerate: " << fpsCalculator.getFps() << " fps";
                }
            }
            lastFrameWasEmpty = emptyFrame;
        }
        clientAvailable = !context->IsCancelled() && clientAvailable;
    }
    return Status::OK;
}

PixelFormat fromProtobuf(const ImageFormat_ImgFormat format) {
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

Status DisplayServiceImpl::getScreenshot(ServerContext* context, const ImageFormat* request,
                                         Image* reply) {
    AEMU_SCOPED_TRACE_CALL();

    auto screen = mMultiDisplay->getDisplay(request->display());
    if (!screen.ok()) {
        LOG(INFO) << "Unable to retrieve display: " << screen.status();
        return abslStatusToGrpcStatus(screen.status());
    }
    auto display = screen->lock();
    if (!display) {
        return Status(grpc::StatusCode::UNAVAILABLE, "Display is no longer active.");
    }

    int desiredWidth = request->width();
    int desiredHeight = request->height();

    DeviceRotation deviceRotation;
    // Let's get sensor data about our location
    auto weak = mRegistry->activeDevice<ISensorDevice>();
    if (auto sensor = weak.lock()) {
        auto possibleRotation = sensor->getDeviceRotation();
        if (!possibleRotation.ok()) {
            VLOG(1) << "Unable to retrieve rotation information due to: "
                    << possibleRotation.status();
        } else {
            deviceRotation = possibleRotation.value();
        }
    }

    // User wants to use device width/height
    if (desiredWidth == 0 || desiredHeight == 0) {
        desiredWidth = display->width();
        desiredHeight = display->height();

        // Make sure they are in the right direction based on layout
        if (deviceRotation.rotation == DeviceSkinRotation::LANDSCAPE ||
            deviceRotation.rotation == DeviceSkinRotation::REVERSE_LANDSCAPE) {
            std::swap(desiredWidth, desiredHeight);
        }
    }

    uint32_t width = display->width();
    uint32_t height = display->height();

    // Depending on the rotation state width and height need to be
    // reversed. as our apsect ration depends on how we are holding our
    // phone..
    if (deviceRotation.rotation == DeviceSkinRotation::LANDSCAPE ||
        deviceRotation.rotation == DeviceSkinRotation::REVERSE_LANDSCAPE) {
        VLOG(2) << "Swapping width & height " << width << "x" << height << " to " << height << "x"
                << width;
        std::swap(width, height);

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
    int rotationDeg = 0;
    char* unsafe = reply->mutable_image()->data();
    uint8_t* pixels = reinterpret_cast<uint8_t*>(unsafe);
    size_t cPixels = reply->mutable_image()->size();
    PixelFormat format = fromProtobuf(request->format());

    auto seq = display->getPixels(format, newWidth, newHeight, rotationDeg, pixels, &cPixels);
    if (absl::IsFailedPrecondition(seq.status())) {
        VLOG(2) << "Allocating string object: " << seq.status();
        auto buffer = new std::string(cPixels, 0);

        // The protobuf message takes ownership of the pointer.
        reply->set_allocated_image(buffer);
        unsafe = reply->mutable_image()->data();
        uint8_t* pixels = reinterpret_cast<uint8_t*>(unsafe);
        size_t cPixels = reply->mutable_image()->size();
        seq = display->getPixels(format, newWidth, newHeight, rotationDeg, pixels, &cPixels);
    }

    if (!seq.status().ok()) {
        return abslStatusToGrpcStatus(seq.status());
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

Status DisplayServiceImpl::getDisplayConfigurations(IMultiDisplay* multiDisplay,
                                                    DisplayConfigurations* reply) {
    for (const auto& weakdisplay : multiDisplay->displays()) {
        if (auto display = weakdisplay.lock()) {
            auto cfg = reply->add_displays();
            // cfg->set_width(1080);
            // cfg->set_height(2400);
            cfg->set_width(display->width());
            cfg->set_height(display->height());
            cfg->set_dpi(display->dpi());
            cfg->set_display(display->id());
            cfg->set_flags(display->flags());
        }
    }

    // TODO(jansene): Where should these really come from?
    reply->set_maxdisplays(multiDisplay->maxDisplays);
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
