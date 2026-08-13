// Copyright 2025 The Android Open Source Project
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
#include "emulator/plugin/vminterface/test/vm_mock.h"

#include <stdbool.h>

// Global variables for mock functions
static RunState runstate_get_value = RUN_STATE_DEBUG;
static ShutdownCause mock_shutdown_cause = SHUTDOWN_CAUSE_NONE;
static bool mock_iothread_locked = false;

const char* error_get_pretty(const Error* err) {
    return "error_get_pretty";
}
void error_free(Error* err) {}

// Getter and setter for RunState
RunState runstate_get() {
    return runstate_get_value;
}

void mock_runstate_set(RunState state) {
    runstate_get_value = state;
}

// Getter and setter for ShutdownCause
ShutdownCause mock_shutdown_cause_get() {
    return mock_shutdown_cause;
}

void mock_shutdown_cause_set(ShutdownCause cause) {
    mock_shutdown_cause = cause;
}

// Getter and setter for iothread_locked
bool mock_iothread_locked_get() {
    return mock_iothread_locked;
}

void mock_iothread_locked_set(bool locked) {
    mock_iothread_locked = locked;
}

// Implementations of the mock functions

int vm_stop(RunState state) {
    mock_runstate_set(state);
    return 0;
}

void vm_start() {
    mock_runstate_set(RUN_STATE_RUNNING);
}

void qemu_system_reset_request(ShutdownCause cause) {
    mock_shutdown_cause_set(cause);
}

int vm_shutdown() {
    mock_runstate_set(RUN_STATE_SHUTDOWN);
    return 0;
}

void qemu_system_shutdown_request(ShutdownCause reason) {
    mock_shutdown_cause_set(reason);
}

// NOLINTBEGIN(readability-identifier-naming)
bool bql_locked(void) {
    return mock_iothread_locked_get();
}

void bql_lock_impl(const char* file, int line) {
    (void)file;
    (void)line;
    mock_iothread_locked_set(true);
}

void bql_unlock(void) {
    mock_iothread_locked_set(false);
}
// NOLINTEND(readability-identifier-naming)

bool save_snapshot(const char* name, bool overwrite, const char* vmstate, bool has_devices,
                   strList* devices, Error** errp) {
    *errp = NULL;
    return false;
}

bool load_snapshot(const char* name, const char* vmstate, bool has_devices, strList* devices,
                   Error** errp) {
    *errp = NULL;
    return false;
}

void load_snapshot_resume(RunState state) {}

bool delete_snapshot(const char* name, bool has_devices, strList* devices, Error** errp) {
    *errp = NULL;
    return false;
}

BlockDriverState* bdrv_all_find_vmstate_bs(const char* vmstate_bs, bool has_devices,
                                           strList* devices, Error** errp) {
    *errp = NULL;
    return NULL;
}

int bdrv_snapshot_list(BlockDriverState* bs, QEMUSnapshotInfo** psn_info) {
    *psn_info = NULL;
    return -1;
}
