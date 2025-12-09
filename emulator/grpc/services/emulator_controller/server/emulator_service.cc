// Copyright (C) 2018 The Android Open Source Project
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

#include "android/emulation/control/emulator_service.h"

#include <grpcpp/grpcpp.h>

#include <memory>

#include "absl/log/log.h"

#include "android/emulation/control/absl_status_translate.h"
#include "android/emulation/control/event_sender.h"
#include "android/emulation/control/keyboard/key_event_sender.h"
#include "android/goldfish/hardware_config.h"
#include "android/goldfish/vm_interface.h"
#include "emulator/grpc/services/emulator_controller/server/clipboard_service.h"
#include "emulator/grpc/services/emulator_controller/server/display_service.h"
#include "emulator/grpc/services/emulator_controller/server/gps_service.h"
#include "emulator/grpc/services/emulator_controller/server/notification_stream_writer.h"
#include "emulator/grpc/services/emulator_controller/server/sensor_service.h"
#include "emulator/grpc/services/emulator_controller/server/status_service.h"
#include "emulator/grpc/services/emulator_controller/server/vm_service.h"
#include "emulator_controller.grpc.pb.h"
#include "goldfish/async/event_loop.h"

namespace android {
namespace emulation {
namespace control {

using ::goldfish::avd_info::AvdUniverse;
using ::goldfish::avd_universe::grpc::GrpcNotificationEventSource;
using ::goldfish::display::IMultiDisplay;
using ::google::protobuf::Empty;
using grpc::ServerContext;
using grpc::Status;

// Logic and data behind the server's behavior.
class EmulatorControllerImpl final
        : public EmulatorController::WithCallbackMethod_streamClipboard<
                  EmulatorController::WithCallbackMethod_streamInputEvent<
                          EmulatorController::WithCallbackMethod_streamClipboard<
                                  EmulatorController::WithCallbackMethod_injectWheel<
                                          EmulatorController::WithCallbackMethod_streamNotification<
                                                  EmulatorController::Service>>>>> {
  public:
    EmulatorControllerImpl(VmOperations* vm, QemuConsole* keyboardConsole, AvdUniverse* avdUniverse,
                           IMultiDisplay* multidisplay, ::goldfish::async::EventLoop* qemuLoop)
            : mVmService(vm)
            , mGrpcNotificationChannel(avdUniverse->getGrpcNotificationChannel())
            , mKeyEventSender(keyboard::createKeyEventSender(keyboardConsole, qemuLoop))
            , mStatusService(avdUniverse->getGuestStatus(), avdUniverse->props().avd_api,
                             avdUniverse->props().hw_config)
            , mSensorService(avdUniverse->getSensorsPhysicalModel())
            , mGpsService(avdUniverse->getLocation())
            , mClipboardService(avdUniverse->getClipboardChannel())
            , mInputEventSender(multidisplay)
            , mDisplayService(multidisplay, &avdUniverse->getSensorsPhysicalModel()) {}

    Status getStatus(ServerContext* /*context*/, const Empty* /*request*/,
                     EmulatorStatus* reply) override {
        return mStatusService.getStatus(reply);
    }

    Status setVmState(ServerContext* /*context*/, const VmRunState* request,
                      Empty* /*reply*/) override {
        return mVmService.setVmState(*request);
    }

    Status getVmState(ServerContext* /*context*/, const Empty* /*request*/,
                      VmRunState* reply) override {
        return mVmService.getVmState(reply);
    }

    Status getGps(ServerContext* /*context*/, const Empty* /*request*/, GpsState* reply) override {
        return mGpsService.getGps(reply);
    }

    Status setGps(ServerContext* context, const GpsState* request, Empty* /*reply*/) override {
        return mGpsService.setGps(*request);
    }

    Status setSensor(ServerContext* /*context*/, const SensorValue* request,
                     Empty* /*reply*/) override {
        return mSensorService.setSensor(*request);
    }

    Status getSensor(ServerContext* /*context*/, const SensorValue* request,
                     SensorValue* reply) override {
        return mSensorService.getSensor(*request, reply);
    }

    ::grpc::ServerWriteReactor<ClipData>* streamClipboard(::grpc::CallbackServerContext* context,
                                                          const Empty* /*request*/) override {
        return mClipboardService.streamClipboard(ClipboardServiceImpl::getPeerId(*context));
    }

    Status sendKey(ServerContext* context, const KeyboardEvent* request,
                   Empty* /*reply*/) override {
        mKeyEventSender->send(*request);
        return Status::OK;
    }

    Status sendMouse(ServerContext* context, const MouseEvent* request, Empty* /*reply*/) override {
        return abslStatusToGrpcStatus(mInputEventSender.send(*request));
    }

    Status sendTouch(ServerContext* context, const TouchEvent* request, Empty* /*reply*/) override {
        return abslStatusToGrpcStatus(mInputEventSender.send(*request));
    }

    ::grpc::ServerReadReactor<WheelEvent>* injectWheel(::grpc::CallbackServerContext* /*context*/,
                                                       Empty* /*response*/) override {
        return new SimpleServerLambdaReader<WheelEvent>([this](auto request) {
            return abslStatusToGrpcStatus(mInputEventSender.send(*request));
        });
    }

    ::grpc::ServerReadReactor<InputEvent>* streamInputEvent(
            ::grpc::CallbackServerContext* /*context*/, Empty* /*response*/) override {
        SimpleServerLambdaReader<InputEvent>* eventReader =
                new SimpleServerLambdaReader<InputEvent>([this](auto request) -> grpc::Status {
                    VLOG(2) << "InputEvent:" << request->ShortDebugString();
                    absl::Status status;
                    if (request->has_key_event()) {
                        mKeyEventSender->send(request->key_event());
                        status = absl::OkStatus();
                    } else if (request->has_mouse_event()) {
                        status = mInputEventSender.send(request->mouse_event());
                    } else if (request->has_touch_event()) {
                        status = mInputEventSender.send(request->touch_event());
                    } else if (request->has_android_event()) {
                        status = mInputEventSender.send(request->android_event());
                    } else if (request->has_pen_event()) {
                        status = mInputEventSender.send(request->pen_event());
                    } else if (request->has_wheel_event()) {
                        status = mInputEventSender.send(request->wheel_event());
                    } else {
                        status = absl::InvalidArgumentError(
                                "Unknown event, is the emulator out of date?.");
                    }
                    return abslStatusToGrpcStatus(status);
                });
        // Note that the event reader will delete itself on completion of
        // the request.
        return eventReader;
    }

    Status getClipboard(ServerContext* context, const Empty* /*request*/,
                        ClipData* reply) override {
        return mClipboardService.getClipboard(reply);
    }

    Status setClipboard(ServerContext* context, const ClipData* request,
                        Empty* /*reply*/) override {
        return mClipboardService.setClipboard(ClipboardServiceImpl::getPeerId(*context), *request);
    }

    Status getDisplayConfigurations(ServerContext* context,
                                    const ::google::protobuf::Empty* request,
                                    DisplayConfigurations* reply) override {
        return mDisplayService.getDisplayConfigurations(context, request, reply);
    }

    Status streamScreenshot(ServerContext* context, const ImageFormat* request,
                            grpc::ServerWriter<Image>* writer) override {
        return mDisplayService.streamScreenshot(context, request, writer);
    }

    Status getScreenshot(ServerContext* context, const ImageFormat* request,
                         Image* reply) override {
        return mDisplayService.getScreenshot(context, request, reply);
    }

    ::grpc::ServerWriteReactor<Notification>* streamNotification(
            ::grpc::CallbackServerContext* /*context*/, const Empty* /*request*/) override {
        return new NotificationStreamWriter(&mGrpcNotificationChannel);
    }

  private:
    VmServiceImpl mVmService;
    GrpcNotificationEventSource& mGrpcNotificationChannel;
    const std::unique_ptr<keyboard::IKeyEventSender> mKeyEventSender;
    StatusServiceImpl mStatusService;
    SensorServiceImpl mSensorService;
    GpsServiceImpl mGpsService;
    ClipboardServiceImpl mClipboardService;
    InputEventSender mInputEventSender;
    DisplayServiceImpl mDisplayService;
};

std::shared_ptr<grpc::Service> getEmulatorController(VmOperations* vm, QemuConsole* keyboardConsole,
                                                     AvdUniverse* avdUniverse,
                                                     IMultiDisplay* multidisplay,
                                                     ::goldfish::async::EventLoop* qemuLoop) {
    return std::make_shared<EmulatorControllerImpl>(vm, keyboardConsole, avdUniverse, multidisplay,
                                                    qemuLoop);
}

}  // namespace control
}  // namespace emulation
}  // namespace android
