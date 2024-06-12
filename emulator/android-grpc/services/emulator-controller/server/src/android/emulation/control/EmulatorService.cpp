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
#include "aemu/base/Log.h"
#include "aemu/base/process/Process.h"
#include "android/emulation/control/display/DisplayChangeListener.h"
#include "hardware/generic/goldfish/emulator/android-grpc/services/emulator-controller/proto/emulator_controller.grpc.pb.h"
#include "host-common/vm_operations.h"
#include <chrono>
#include <grpc++/grpc++.h>
#include <grpcpp/support/status.h>

namespace android {
namespace emulation {
namespace control {

using grpc::ServerContext;
using grpc::Status;

// Logic and data behind the server's behavior.
class EmulatorControllerImpl final
    : public EmulatorController::WithCallbackMethod_streamScreenshot<
          EmulatorController::Service> {
public:
  EmulatorControllerImpl(const QAndroidVmOperations *vm,
                         DisplayChangeListener *displayChangeListener)
      : mVm(vm), mDisplayChangeListener(displayChangeListener) {}

  Status getDisplayConfigurations(ServerContext *context,
                                  const ::google::protobuf::Empty *request,
                                  DisplayConfigurations *reply) override {
    return Status(::grpc::StatusCode::FAILED_PRECONDITION,
                  "The multi-display feature is not available", "");
  }

  Status setVmState(ServerContext *context, const VmRunState *request,
                    ::google::protobuf::Empty *reply) override {
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

  Status getVmState(ServerContext *context,
                    const ::google::protobuf::Empty *request,
                    VmRunState *reply) override {
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

  ::grpc::ServerWriteReactor<Image> *
  streamScreenshot(::grpc::CallbackServerContext * /*context*/,
                   const ImageFormat *request) override {
    LOG(INFO) << "streamScreenshot";
    auto listener = mDisplayChangeListener->addListener(*request);
    LOG(INFO) << "listener registered";
    auto writer = new GenericEventStreamWriter<Image>(listener);
    writer->eventArrived(mDisplayChangeListener->getScreenshot(*request));
    return writer;
  }

private:
  const QAndroidVmOperations *mVm;
  DisplayChangeListener *mDisplayChangeListener;
};

grpc::Service *
getEmulatorController(const QAndroidVmOperations *vm,
                      DisplayChangeListener *displayChangeListener) {
  return new EmulatorControllerImpl(vm, displayChangeListener);
}

} // namespace control
} // namespace emulation
} // namespace android
