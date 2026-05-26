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

#include "hugepage_device.h"

#ifdef __linux__
#include <unistd.h>

#include <fstream>
#include <sstream>

#include "absl/log/absl_log.h"
#include "absl/strings/numbers.h"

#include "android/goldfish/avd.h"
#include "android/goldfish/hardware_config.h"
#endif

namespace android::goldfish {

std::vector<std::string> HugePageDevice::getQemuParameters(
        const EmulatorConfig& config) const {
    std::vector<std::string> params;

#ifdef __linux__
    bool ready = false;

    int memorySizeMiB = config.avd().Hw().hw_ramSize;
    if (memorySizeMiB <= 0) {
        memorySizeMiB = 2048;
    }
    if (config.opts().memory != nullptr) {
        if (!absl::SimpleAtoi(config.opts().memory, &memorySizeMiB)) {
            ABSL_LOG(WARNING) << "Failed to parse memory option: " << config.opts().memory;
        }
    }

    // Add 100MB buffer for QEMU overhead, each page is 2MB.
    int required_hugepages = (memorySizeMiB + 100) / 2;
    int free_hugepages = 0;

    if (access("/dev/hugepages", R_OK | W_OK) == 0) {
        std::ifstream meminfo("/proc/meminfo");
        std::string line;
        while (std::getline(meminfo, line)) {
            if (line.find("HugePages_Free:") == 0) {
                std::istringstream iss(line.substr(15));
                iss >> free_hugepages;
                break;
            }
        }

        if (free_hugepages >= required_hugepages) {
            ready = true;
        }
    }

    if (ready) {
        params.push_back("-mem-path");
        params.push_back("/dev/hugepages");
        params.push_back("-mem-prealloc");
    } else {
        ABSL_LOG(WARNING)
                << "Host is not ready for HugePages or insufficient free hugepages (Needed "
                << required_hugepages << ", but found " << free_hugepages
                << "). To enable it, run:\n"
                << "  sudo sysctl vm.nr_hugepages=<num_pages>\n"
                << "  sudo mkdir -p /dev/hugepages\n"
                << "  sudo mount -t hugetlbfs nodev /dev/hugepages\n"
                << "  sudo chown $USER:$USER /dev/hugepages";
    }
#endif

    return params;
}

}  // namespace android::goldfish