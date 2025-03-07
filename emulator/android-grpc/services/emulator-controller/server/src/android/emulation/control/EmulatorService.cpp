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

#include "aemu/base/process/Process.h"
#include "android/emulation/control/ClipboardService.h"
#include "android/emulation/control/DisplayService.h"
#include "android/emulation/control/GpsService.h"
#include "android/emulation/control/SensorService.h"
#include "android/emulation/control/StatusService.h"
#include "android/emulation/control/display/DisplayChangeListener.h"
#include "android/emulation/control/input/EventSender.h"
#include "android/emulation/control/keyboard/KeyEventSender.h"
#include "android/grpc/utils/AbslStatusTranslate.h"
#include "hardware/generic/goldfish/emulator/android-grpc/services/emulator-controller/proto/emulator_controller.grpc.pb.h"
#include "host-common/vm_operations.h"

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
                                      EmulatorController::Service>>>> {
  public:
    EmulatorControllerImpl(const QAndroidVmOperations* vm, ConnectorRegistry* connectorRegistry,
                           android::goldfish::Avd* avd, IMultiDisplay* multidisplay)
        : mVm(vm),
          mSensorService(connectorRegistry),
          mClipboardService(connectorRegistry),
          mDisplayService(multidisplay, connectorRegistry),
          mStatusService(connectorRegistry, avd),
          mKeyEventSender(keyboard::createKeyEventSender(qemu_console_lookup_by_index(0))),
          mGpsService(connectorRegistry),
          mInputEventSender(multidisplay) {}

    Status getDisplayConfigurations(ServerContext* context,
                                    const ::google::protobuf::Empty* request,
                                    DisplayConfigurations* reply) override {
        return Status(::grpc::StatusCode::FAILED_PRECONDITION,
                      "The multi-display feature is not available", "");
    }

    Status getStatus(ServerContext* context, const ::google::protobuf::Empty* request,
                     EmulatorStatus* reply) override {
        return mStatusService.getStatus(context, request, reply);
    }

    Status setVmState(ServerContext* context, const VmRunState* request,
                      ::google::protobuf::Empty* reply) override {
        const std::chrono::milliseconds kWaitToDie = std::chrono::seconds(60);

        // These need to happen on the qemu looper as these transitions
        // will require io locks.
        auto state = request->state();
        switch (state) {
            case VmRunState::RESET:
                mVm->vmReset();
                break;
            case VmRunState::SHUTDOWN:
                mVm->vmShutdown();
                break;
            case VmRunState::TERMINATE: {
                LOG(INFO) << "Terminating the emulator.";
                android::base::Process::me()->terminate();
            }; break;
            case VmRunState::PAUSED:
                mVm->vmPause();
                break;
            case VmRunState::RUNNING:
                mVm->vmResume();
                break;
            case VmRunState::RESTART:
                mVm->vmReset();
                break;
            case VmRunState::START:
                mVm->vmStart();
                break;
            case VmRunState::STOP:
                mVm->vmStop();
                break;
            default:
                break;
        };

        return Status::OK;
    }

    Status getVmState(ServerContext* context, const ::google::protobuf::Empty* request,
                      VmRunState* reply) override {
        switch (mVm->getRunState()) {
            case QEMU_RUN_STATE_PAUSED:
            case QEMU_RUN_STATE_SUSPENDED:
                reply->set_state(VmRunState::PAUSED);
                break;
            case QEMU_RUN_STATE_RESTORE_VM:
                reply->set_state(VmRunState::RESTORE_VM);
                break;
            case QEMU_RUN_STATE_RUNNING:
                reply->set_state(VmRunState::RUNNING);
                break;
            case QEMU_RUN_STATE_SAVE_VM:
                reply->set_state(VmRunState::SAVE_VM);
                break;
            case QEMU_RUN_STATE_SHUTDOWN:
                reply->set_state(VmRunState::SHUTDOWN);
                break;
            case QEMU_RUN_STATE_GUEST_PANICKED:
            case QEMU_RUN_STATE_INTERNAL_ERROR:
            case QEMU_RUN_STATE_IO_ERROR:
                reply->set_state(VmRunState::INTERNAL_ERROR);
                break;
            default:
                reply->set_state(VmRunState::UNKNOWN);
                break;
        };

        return Status::OK;
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

    Status streamScreenshot(ServerContext* context, const ImageFormat* request,
                            grpc::ServerWriter<Image>* writer) override {
        return mDisplayService.streamScreenshot(context, request, writer);
    }

    Status getScreenshot(ServerContext* context, const ImageFormat* request,
                         Image* reply) override {
        return mDisplayService.getScreenshot(context, request, reply);
    }

  private:
    const QAndroidVmOperations* mVm;
    SensorServiceImpl mSensorService;
    ClipboardServiceImpl mClipboardService;
    DisplayServiceImpl mDisplayService;
    StatusServiceImpl mStatusService;
    std::unique_ptr<keyboard::IKeyEventSender> mKeyEventSender;
    GpsServiceImpl mGpsService;
    InputEventSender mInputEventSender;
};

grpc::Service* getEmulatorController(const QAndroidVmOperations* vm,
                                     ConnectorRegistry* connectorRegistry,
                                     android::goldfish::Avd* avd, IMultiDisplay* multidisplay) {
    return new EmulatorControllerImpl(vm, connectorRegistry, avd, multidisplay);
}

}  // namespace control
}  // namespace emulation
}  // namespace android
