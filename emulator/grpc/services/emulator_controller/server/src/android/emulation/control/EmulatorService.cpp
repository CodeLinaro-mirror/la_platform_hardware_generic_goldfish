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
#include "android/emulation/control/EmulatorService.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <memory>

#include "absl/log/log.h"

#include "android/emulation/control/ClipboardService.h"
#include "android/emulation/control/DisplayService.h"
#include "android/emulation/control/GpsService.h"
#include "android/emulation/control/NotificationStream.h"
#include "android/emulation/control/SensorService.h"
#include "android/emulation/control/StatusService.h"
#include "android/emulation/control/VmService.h"
#include "android/emulation/control/input/EventSender.h"
#include "android/emulation/control/keyboard/KeyEventSender.h"
#include "android/goldfish/vm/VmInterface.h"
#include "android/grpc/utils/AbslStatusTranslate.h"
#include "goldfish/async/event_loop.h"
#include "hardware/generic/goldfish/emulator/grpc/services/emulator_controller/proto/emulator_controller.grpc.pb.h"

extern "C" {
QemuConsole* qemu_console_lookup_by_index(unsigned int index);
}

namespace android {
namespace emulation {
namespace control {

using ::android::goldfish::IMultiDisplay;
using ::goldfish::devices::ConnectorRegistry;
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
    EmulatorControllerImpl(VmOperations* vm, ConnectorRegistry* connectorRegistry,
                           android::goldfish::Avd* avd, IMultiDisplay* multidisplay,
                           ::goldfish::async::EventLoop* qemuLoop)
            : mKeyEventSender(
                      keyboard::createKeyEventSender(qemu_console_lookup_by_index(0), qemuLoop))
            , mNotificationStream(NotificationStream::create(multidisplay, connectorRegistry))
            , mClipboardService(connectorRegistry)
            , mDisplayService(multidisplay, connectorRegistry)
            , mGpsService(connectorRegistry)
            , mInputEventSender(multidisplay)
            , mSensorService(connectorRegistry)
            , mStatusService(connectorRegistry, avd)
            , mVmService(vm) {}

    Status getStatus(ServerContext* context, const ::google::protobuf::Empty* request,
                     EmulatorStatus* reply) override {
        return mStatusService.getStatus(context, request, reply);
    }

    Status setVmState(ServerContext* context, const VmRunState* request,
                      ::google::protobuf::Empty* reply) override {
        return mVmService.setVmState(context, request, reply);
    }

    Status getVmState(ServerContext* context, const ::google::protobuf::Empty* request,
                      VmRunState* reply) override {
        return mVmService.getVmState(context, request, reply);
    }

    Status getGps(ServerContext* context, const Empty* request, GpsState* reply) {
        return mGpsService.getGps(context, request, reply);
    }

    Status setGps(ServerContext* context, const GpsState* request, Empty* reply) {
        return mGpsService.setGps(context, request, reply);
    }

    Status setSensor(ServerContext* context, const SensorValue* request,
                     ::google::protobuf::Empty* reply) override {
        return mSensorService.setSensor(context, request, reply);
    }

    Status getSensor(ServerContext* context, const SensorValue* request,
                     SensorValue* reply) override {
        return mSensorService.getSensor(context, request, reply);
    }

    ::grpc::ServerWriteReactor<ClipData>* streamClipboard(
            ::grpc::CallbackServerContext* context,
            const ::google::protobuf::Empty* request) override {
        return mClipboardService.streamClipboard(context, request);
    }

    Status sendKey(ServerContext* context, const KeyboardEvent* request,
                   ::google::protobuf::Empty* reply) override {
        mKeyEventSender->send(*request);
        return Status::OK;
    }

    Status sendMouse(ServerContext* context, const MouseEvent* request,
                     ::google::protobuf::Empty* reply) override {
        return abslStatusToGrpcStatus(mInputEventSender.send(*request));
    }

    Status sendTouch(ServerContext* context, const TouchEvent* request,
                     ::google::protobuf::Empty* reply) override {
        return abslStatusToGrpcStatus(mInputEventSender.send(*request));
    }

    ::grpc::ServerReadReactor<WheelEvent>* injectWheel(
            ::grpc::CallbackServerContext* /*context*/,
            ::google::protobuf::Empty* /*response*/) override {
        return new SimpleServerLambdaReader<WheelEvent>(
                [this](auto request) { (void)mInputEventSender.send(*request); });
    }

    ::grpc::ServerReadReactor<InputEvent>* streamInputEvent(
            ::grpc::CallbackServerContext* /*context*/,
            ::google::protobuf::Empty* /*response*/) override {
        SimpleServerLambdaReader<InputEvent>* eventReader =
                new SimpleServerLambdaReader<InputEvent>([this, &eventReader](auto request) {
                    VLOG(1) << "InputEvent:" << request->ShortDebugString();
                    absl::Status status = absl::OkStatus();
                    if (request->has_key_event()) {
                        mKeyEventSender->send(request->key_event());
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
                        // Mark the stream as completed, this will
                        // result in setting that status and scheduling
                        // of a completion (onDone) event the async
                        // queue.
                        eventReader->Finish(Status(::grpc::StatusCode::INVALID_ARGUMENT,
                                                   "Unknown event, is the emulator out of date?."));
                    }
                    if (!status.ok()) {
                        eventReader->Finish(abslStatusToGrpcStatus(status));
                    }
                });
        // Note that the event reader will delete itself on completion of
        // the request.
        return eventReader;
    }

    Status getClipboard(ServerContext* context, const ::google::protobuf::Empty* request,
                        ClipData* reply) override {
        return mClipboardService.getClipboard(context, request, reply);
    }

    Status setClipboard(ServerContext* context, const ClipData* request,
                        ::google::protobuf::Empty* reply) override {
        return mClipboardService.setClipboard(context, request, reply);
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
            ::grpc::CallbackServerContext* context,
            const ::google::protobuf::Empty* request) override {
        return mNotificationStream->notificationStream();
    }

  private:
    const std::unique_ptr<keyboard::IKeyEventSender> mKeyEventSender;
    const std::shared_ptr<NotificationStream> mNotificationStream;
    ClipboardServiceImpl mClipboardService;
    DisplayServiceImpl mDisplayService;
    GpsServiceImpl mGpsService;
    InputEventSender mInputEventSender;
    SensorServiceImpl mSensorService;
    StatusServiceImpl mStatusService;
    VmServiceImpl mVmService;
};

grpc::Service* getEmulatorController(VmOperations* vm, ConnectorRegistry* connectorRegistry,
                                     android::goldfish::Avd* avd, IMultiDisplay* multidisplay,

                                     ::goldfish::async::EventLoop* qemuLoop) {
    return new EmulatorControllerImpl(vm, connectorRegistry, avd, multidisplay, qemuLoop);
}

}  // namespace control
}  // namespace emulation
}  // namespace android
