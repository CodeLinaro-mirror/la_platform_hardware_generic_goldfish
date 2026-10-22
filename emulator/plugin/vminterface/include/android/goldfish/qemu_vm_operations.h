// Copyright 2026 The Android Open Source Project
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

#include <memory>

#include "android/goldfish/vm_interface.h"
#include "goldfish/async/event_loop.h"

namespace android {
namespace goldfish {

/**
 * @brief Creates the VmOperations implementation that controls the QEMU VM.
 *
 * The instance registers a QEMU run-state handler when constructed and
 * unregisters it when destroyed, both under the VM lock. Create it once the VM
 * lock is usable (for example while devices are realized) and keep it alive
 * for as long as consumers hold references to it.
 *
 * Mutating and snapshot operations are dispatched synchronously to `qemu_loop`
 * via `PostAndWait`. They are expected to be called from external threads
 * (such as gRPC server or worker threads) and will fatally abort if called
 * from either `event_loop` or `qemu_loop`. Read-only queries (`isRunning`,
 * `getRunState`, `getConfiguration`) execute directly without posting to
 * `qemu_loop` and may be called from any thread.
 *
 * @param event_loop The loop that runs the `RunStateEvents()` callbacks. Must
 *        be distinct from `qemu_loop` and outlive the returned instance.
 * @param qemu_loop The QEMU main event loop used to execute VM operations. Must
 *        outlive the returned instance.
 * @return The QEMU-backed VmOperations.
 */
std::unique_ptr<VmOperations> CreateQemuVmOperations(::goldfish::async::EventLoop* event_loop,
                                                     ::goldfish::async::EventLoop* qemu_loop);

}  // namespace goldfish
}  // namespace android
