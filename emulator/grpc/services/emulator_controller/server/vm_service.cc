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
#include "emulator/grpc/services/emulator_controller/server/vm_service.h"

#include "absl/log/log.h"

#include "aemu/base/process/Process.h"

namespace android {
namespace emulation {
namespace control {

Status VmServiceImpl::setVmState(const VmRunState& request) {
    auto state = request.state();
    switch (state) {
    case VmRunState::RESET:
        mVm->reset();
        break;
    case VmRunState::SHUTDOWN:
        mVm->shutdown();
        break;
    case VmRunState::TERMINATE: {
        LOG(ERROR) << "Received request to terminate the emulator process immediately. No "
                      "cleanup will be performed.";
        android::base::Process::me()->terminate();
    }; break;
    case VmRunState::PAUSED:
        mVm->pause();
        break;
    case VmRunState::RUNNING:
        mVm->resume();
        break;
    case VmRunState::RESTART:
        mVm->reset();
        break;
    case VmRunState::START:
        mVm->start();
        break;
    case VmRunState::STOP:
        mVm->stop();
        break;
    default:
        break;
    };

    return Status::OK;
}

Status VmServiceImpl::getVmState(VmRunState* reply) {
    using ::android::goldfish::EmuRunState;

    auto state = mVm->getRunState();
    VLOG(1) << "Current emulator run state: " << static_cast<int>(state) << " (" << state << ")";
    switch (state) {
    case EmuRunState::Paused:
    case EmuRunState::Suspended:
        reply->set_state(VmRunState::PAUSED);
        break;
    case EmuRunState::Running:
        reply->set_state(VmRunState::RUNNING);
        break;
    case EmuRunState::SaveVm:
        reply->set_state(VmRunState::SAVE_VM);
        break;
    case EmuRunState::Shutdown:
        reply->set_state(VmRunState::SHUTDOWN);
        break;
    case EmuRunState::GuestPanicked:
    case EmuRunState::InternalError:
    case EmuRunState::IoError:
        reply->set_state(VmRunState::INTERNAL_ERROR);
        break;
    case EmuRunState::InMigrate:
    case EmuRunState::PostMigrate:
    case EmuRunState::FinishMigrate:
    case EmuRunState::RestoreVm:
        reply->set_state(VmRunState::RESTORE_VM);
        break;
    case EmuRunState::Debug:
    case EmuRunState::PreLaunch:
    case EmuRunState::Watchdog:
    case EmuRunState::Colo:
        reply->set_state(VmRunState::UNKNOWN);
        break;
    default:
        LOG(ERROR) << "Encountered an unexpected emulator run state: " << static_cast<int>(state)
                   << " (" << state << ") "
                   << ". This state is not mapped to a gRPC VmRunState.";
        reply->set_state(VmRunState::UNKNOWN);
        break;
    };

    return Status::OK;
}

}  // namespace control
}  // namespace emulation
}  // namespace android
