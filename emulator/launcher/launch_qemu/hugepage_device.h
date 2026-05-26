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

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "device.h"
#include "emulator_config.h"

namespace android::goldfish {

/**
 * @brief Configures QEMU to use hugepages for guest RAM.
 *
 * Using hugepages significantly improves memory access performance,
 * particularly in nested virtualization environments, by reducing TLB misses
 * and page fault overhead during operations like snapshot loading (loadvm).
 *
 * To enable this on the host Linux system, you must allocate hugepages and
 * ensure the mount point exists and has the correct permissions:
 *
 *   # Allocate enough 2MB hugepages (e.g., 5376 pages for ~10.5 GB)
 *   sudo sysctl vm.nr_hugepages=5376
 *
 *   # Ensure the hugepages mount point exists
 *   sudo mkdir -p /dev/hugepages
 *   sudo mount -t hugetlbfs nodev /dev/hugepages
 *
 *   # Grant permissions to your user (or use chmod)
 *   sudo chown $USER:$USER /dev/hugepages
 */
class HugePageDevice : public Device {
public:
    HugePageDevice() : Device("hugepage") {}

    std::vector<std::string> getQemuParameters(
            const EmulatorConfig& config) const override;

    absl::Status initialize(const EmulatorConfig& emulator) override {
        return absl::OkStatus();
    }
};

}  // namespace android::goldfish
