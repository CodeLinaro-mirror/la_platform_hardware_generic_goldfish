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

// clang-format off
// IWYU pragma: begin_keep

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "exec/cpu-common.h"
#include "hw/core/cpu.h"
#include "sysemu/cpus.h"
#include "qemu/lockable.h"
#include "qemu/accel.h"
#include "vm/qemu-machine-info.h"
#include "hw/boards.h"
// IWYU pragma: end_keep
// clang-format on

// These functions retrieve machine configuration from qemu.

const char* cpu_type(void) {
    return current_machine->cpu_type;
}

const char* accel_name() {
    return current_accel_name();
}

int cpu_count() {
    // Will be initialized in CPU_FOREACH macro below.
    CPUState* some_cpu;
    int count = 0;
    cpu_list_lock();
    CPU_FOREACH(some_cpu) {
        count++;
    }
    cpu_list_unlock();
    return count;
}
