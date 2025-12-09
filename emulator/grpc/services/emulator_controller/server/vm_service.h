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

#pragma once

#include <grpcpp/grpcpp.h>

#include "android/goldfish/vm_interface.h"
#include "emulator_controller.grpc.pb.h"

namespace android {
namespace emulation {
namespace control {

using ::android::goldfish::VmOperations;
using grpc::Status;

/**
 * @brief Implements the gRPC APIs for controlling the virtual machine (VM).
 *
 * This class provides methods to manage the lifecycle of the virtual machine,
 * including starting, stopping, pausing, resuming, resetting, and shutting down
 * the VM. It also allows querying the current state of the VM.
 *
 * The VmServiceImpl interacts with the underlying VmOperations interface to
 * perform the actual VM operations. It translates the gRPC requests and
 * responses to and from the internal VM state representation.
 */
class VmServiceImpl {
  public:
    VmServiceImpl(VmOperations* vm) : mVm(vm) {}

    /**
     * @brief Gets the current state of the virtual machine.
     *
     * @param context The server context for the gRPC call.
     * @param request An empty request message.
     * @param reply The VmRunState message containing the current VM state.
     * @return A gRPC status indicating the success or failure of the operation.
     */
    Status getVmState(VmRunState* reply);

    /**
     * @brief Sets the desired state of the virtual machine.
     *
     * @note This can terminate the process if VmRunstate is TERMINATE
     *
     * @param context The server context for the gRPC call.
     * @param request The VmRunState message specifying the desired VM state.
     * @param reply An empty response message.
     * @return A gRPC status indicating the success or failure of the operation.
     */
    Status setVmState(const VmRunState& request);

  private:
    VmOperations* mVm;  ///< The VmOperations instance used to control the VM.
};

}  // namespace control
}  // namespace emulation
}  // namespace android
