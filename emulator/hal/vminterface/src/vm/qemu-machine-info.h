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

#include <sys/cdefs.h>

__BEGIN_DECLS
/**
 * @brief Returns the number of available CPUs in the running QEMU instance.
 *
 * This function queries the running QEMU instance to determine the total
 * number of CPUs that are available for use within the virtual machine.
 *
 * @return The number of available CPUs.
 */
int cpu_count_cwrap();

/**
 * @brief Returns the current QEMU CPU type.
 *
 * This function retrieves the specific CPU type that is being emulated
 * by the running QEMU instance. Examples of CPU types include "Cortex-A57",
 * "Snowridge", and "EPYC-Genoa".
 *
 * @return A pointer to a null-terminated string representing the QEMU CPU type.
 *         The string is owned by the QEMU instance and should not be freed.
 */
const char* cpu_type_cwrap();

/**
 * @brief Returns the name of the hypervisor being used by the running QEMU instance.
 *
 * This function queries the running QEMU instance to determine the name of
 * the hypervisor that is providing hardware acceleration. Examples of
 * hypervisor names include "KVM", "HVF", "WHPX", and "tcg".
 *
 * @return A pointer to a null-terminated string representing the hypervisor name.
 *         The string is owned by the QEMU instance and should not be freed.
 */
const char* accel_name_cwrap();

__END_DECLS
