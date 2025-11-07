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

#include "QemuCpuWrapper.h"

#include <stdbool.h>

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
#include "exec/cpu-common.h"
#include "hw/core/cpu.h"
#include "qemu/main-loop.h"
#include "system/cpus.h"
// IWYU pragma: end_keep
// clang-format on

int aemu_cpus_count() {
    return cpus_count();
}

static void aemu_run_on_cpu_func(CPUState* cpu, run_on_cpu_data data) {
    cpus_callback* callback = (cpus_callback*)data.host_ptr;
    callback->f(callback->opaque);
}

bool aemu_cpus_run_async(int i, cpus_callback* callback) {
    CPUState* cpu = qemu_get_cpu(i);
    if (cpu == NULL) {
        return false;
    }

    async_run_on_cpu(cpu, aemu_run_on_cpu_func, RUN_ON_CPU_HOST_PTR(callback));
    return true;
}