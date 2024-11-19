// Copyright 2015 The Android Open Source Project
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
#include "host-common/vm_operations.h"  // for SnapshotCallbacks
#include "qemu/osdep.h"
#include "sysemu/runstate.h"

RunState global_state_get_runstate(void);

static bool qemu_vm_stop() {
    vm_stop(RUN_STATE_PAUSED);
    return true;
}

static bool qemu_vm_start() {
    vm_start();
    return true;
}

static bool qemu_vm_pause() {
    return vm_stop(RUN_STATE_PAUSED);
}

static bool qemu_vm_resume() {
    vm_start();
    return true;
}

static EmuRunState qemu_get_runstate() {
    return (EmuRunState)runstate_get();
};

static void system_reset_request() {
    qemu_system_reset_request(SHUTDOWN_CAUSE_SUBSYSTEM_RESET);
}

void system_shutdown_request() {
    vm_shutdown();
}

static const QAndroidVmOperations sQAndroidVmOperations = {
        .vmStop = qemu_vm_stop,
        .vmStart = qemu_vm_start,
        .vmReset = system_reset_request,
        .vmShutdown = system_shutdown_request,
        .vmPause = qemu_vm_pause,
        .vmResume = qemu_vm_resume,
        .getRunState = qemu_get_runstate,
};

const QAndroidVmOperations* const gQAndroidVmOperations = &sQAndroidVmOperations;
