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
#include "vm_service.h"

#include "absl/log/log.h"

#include "android/process/process.h"

namespace android {
namespace emulation {
namespace control {

Status VmServiceImpl::setVmState(const VmRunState& request) {
    auto state = request.state();
    switch (state) {
    case VmRunState::RESET:
        vm_->Reset();
        break;
    case VmRunState::SHUTDOWN:
        vm_->Shutdown();
        break;
    case VmRunState::TERMINATE: {
        LOG(ERROR) << "Received request to terminate the emulator process immediately. No "
                      "cleanup will be performed.";
        android::base::Process::Me()->Terminate();
    }; break;
    case VmRunState::PAUSED:
        vm_->Pause();
        break;
    case VmRunState::RUNNING:
        vm_->Resume();
        break;
    case VmRunState::RESTART:
        vm_->Reset();
        break;
    case VmRunState::START:
        vm_->Start();
        break;
    case VmRunState::STOP:
        vm_->Stop();
        break;
    default:
        break;
    };

    return Status::OK;
}

Status VmServiceImpl::getVmState(VmRunState* reply) {
    using ::android::goldfish::EmuRunState;

    auto state = vm_->GetRunState();
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
