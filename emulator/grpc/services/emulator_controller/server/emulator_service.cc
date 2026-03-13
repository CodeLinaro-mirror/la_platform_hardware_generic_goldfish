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
#include "emulator/grpc/services/emulator_controller/server/battery_service.h"
#include "emulator/grpc/services/emulator_controller/server/clipboard_service.h"
#include "emulator/grpc/services/emulator_controller/server/display_service.h"
#include "emulator/grpc/services/emulator_controller/server/gps_service.h"
#include "emulator/grpc/services/emulator_controller/server/notification_stream_writer.h"
#include "emulator/grpc/services/emulator_controller/server/notification_store.h"
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
                           IMultiDisplay* multidisplay, ::goldfish::async::EventLoop* qemu_loop)
            : mVmService(vm)
            , mGrpcNotificationChannel(avdUniverse->GetGrpcNotificationChannel())
            , mNotificationStore(&mGrpcNotificationChannel)
            , mKeyEventSender(keyboard::createKeyEventSender(keyboardConsole, qemu_loop))
            , mStatusService(avdUniverse->GetGuestStatus(), avdUniverse->Props().avd_api,
                             avdUniverse->Props().hw_config)
            , mBatteryService(avdUniverse->GetBattery())
            , mSensorService(avdUniverse->GetSensorsPhysicalModel())
            , mGpsService(avdUniverse->GetLocation())
            , mClipboardService(avdUniverse->GetClipboardChannel())
            , mInputEventSender(multidisplay)
            , mDisplayService(multidisplay, &avdUniverse->GetSensorsPhysicalModel()) {}

    Status getBattery(ServerContext* /*context*/, const Empty* /*request*/,
                      BatteryState* reply) override {
        return mBatteryService.getBattery(reply);
    }

    Status setBattery(ServerContext* /*context*/, const BatteryState* request,
                      Empty* /*reply*/) override {
        return mBatteryService.setBattery(*request);
    }

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

    Status setPhysicalModel(ServerContext* /*context*/, const PhysicalModelValue* request,
                            google::protobuf::Empty* /*reply*/) override {
        return mSensorService.setPhysicalModel(*request);
    }

    Status getPhysicalModel(ServerContext* /*context*/, const PhysicalModelValue* request,
                            PhysicalModelValue* reply) override {
        return mSensorService.getPhysicalModel(*request, reply);
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
        return AbslStatusToGrpcStatus(mInputEventSender.Send(*request));
    }

    Status sendTouch(ServerContext* context, const TouchEvent* request, Empty* /*reply*/) override {
        return AbslStatusToGrpcStatus(mInputEventSender.Send(*request));
    }

    ::grpc::ServerReadReactor<WheelEvent>* injectWheel(::grpc::CallbackServerContext* /*context*/,
                                                       Empty* /*response*/) override {
        return new SimpleServerLambdaReader<WheelEvent>([this](auto request) {
            return AbslStatusToGrpcStatus(mInputEventSender.Send(*request));
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
                        status = mInputEventSender.Send(request->mouse_event());
                    } else if (request->has_touch_event()) {
                        status = mInputEventSender.Send(request->touch_event());
                    } else if (request->has_android_event()) {
                        status = mInputEventSender.Send(request->android_event());
                    } else if (request->has_pen_event()) {
                        status = mInputEventSender.Send(request->pen_event());
                    } else if (request->has_wheel_event()) {
                        status = mInputEventSender.Send(request->wheel_event());
                    } else {
                        status = absl::InvalidArgumentError(
                                "Unknown event, is the emulator out of date?.");
                    }
                    return AbslStatusToGrpcStatus(status);
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

    Status setDisplayConfigurations(ServerContext* context, const DisplayConfigurations* request,
                                    DisplayConfigurations* reply) override {
        return mDisplayService.setDisplayConfigurations(context, request, reply);
    }

    Status getDisplayMode(ServerContext* context, const Empty* request,
                          DisplayMode* reply) override {
        return mDisplayService.getDisplayMode(context, request, reply);
    }

    Status setDisplayMode(ServerContext* context, const DisplayMode* request,
                          Empty* reply) override {
        return mDisplayService.setDisplayMode(context, request, reply);
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
        return new NotificationStreamWriter(&mGrpcNotificationChannel, &mNotificationStore);
    }

  private:
    VmServiceImpl mVmService;
    GrpcNotificationEventSource& mGrpcNotificationChannel;
    NotificationStore mNotificationStore;
    const std::unique_ptr<keyboard::IKeyEventSender> mKeyEventSender;
    StatusServiceImpl mStatusService;
    BatteryServiceImpl mBatteryService;
    SensorServiceImpl mSensorService;
    GpsServiceImpl mGpsService;
    ClipboardServiceImpl mClipboardService;
    InputEventSender mInputEventSender;
    DisplayServiceImpl mDisplayService;
};

std::shared_ptr<grpc::Service> getEmulatorController(VmOperations* vm, QemuConsole* keyboardConsole,
                                                     AvdUniverse* avdUniverse,
                                                     IMultiDisplay* multidisplay,
                                                     ::goldfish::async::EventLoop* qemu_loop) {
    return std::make_shared<EmulatorControllerImpl>(vm, keyboardConsole, avdUniverse, multidisplay,
                                                    qemu_loop);
}

}  // namespace control
}  // namespace emulation
}  // namespace android
