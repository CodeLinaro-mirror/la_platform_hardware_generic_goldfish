// Copyright (C) 2025 The Android Open Source Project
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

#include <stdbool.h>
#include <sys/cdefs.h>

__BEGIN_DECLS

int aemu_cpus_count();

typedef void (*Fn)(void* opaque);
typedef struct {
    Fn f;
    void* opaque;
} cpus_callback;

/** Schedules a callback to be executed asynchronously on a specific QEMU vCPU thread.
 *
 * This function can be called from any thread. It does not require the Big QEMU Lock (BQL)
 * to be held when calling. The provided 'callback' will be executed on the QEMU vCPU thread
 * specified by 'cpu_index'.
 *
 * Args:
 *   cpu_index: The index of the CPU core (0 to aemu_cpus_count() - 1) on which the callback should run.
 *   callback: A pointer to a cpus_callback struct containing the function and opaque data. This callback
 *   object must live until the callback has been run.
 *
 * Returns:
 *   true if the callback was successfully scheduled, false otherwise.
 */
bool aemu_cpus_run_async(int cpu_index, cpus_callback* callback);

__END_DECLS