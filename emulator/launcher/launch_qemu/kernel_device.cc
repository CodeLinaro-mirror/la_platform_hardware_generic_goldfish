// Copyright 2024 The Android Open Source Project
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

#include "kernel_device.h"

#include <fstream>
#include <initializer_list>
#include <string>

#include "absl/container/btree_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include "android/cmdline_definitions.h"
#include "android/goldfish/avd.h"
#include "android/status/status_macros.h"
#include "goldfish/file/file.h"

namespace android::goldfish {

namespace {
absl::StatusOr<std::string> command_line(const Avd& avd, const AndroidOptions& opts) {
    // btree to provide deterministic (sorted) order.
    absl::btree_set<std::string> cl = {"bootconfig", "no_timer_check", "8250.nr_uarts=1",
                                       "loop.max_part=7", "mac80211_hwsim.radios=0"};
    // TODO add ramoops args?
    switch (auto a = avd.DetectArchitecture(); a) {
    case Avd::CpuArchitecture::kArm:
        cl.merge(absl::btree_set<std::string>{"console=ttyAMA0,38400", "earlyprintk=ttyAMA0",
                                              "keep_bootcon", "ndns=3"});
        break;
    case Avd::CpuArchitecture::kX86:
        cl.merge(absl::btree_set<std::string>{"console=ttyS0,38400", "earlyprintk=ttyS0",
                                              "clocksource=pit", "memmap=0x10000$0xff018000"});
        break;
    case Avd::CpuArchitecture::kRiscV:
    default:
        return absl::UnimplementedError(absl::StrCat("Machine type not supported: ", a));
    }

    if (opts.shell || opts.shell_serial || opts.show_kernel) {
        cl.insert("printk.devkmsg=on");
    }

    // Note that this is currently duplicating: 8250.nr_uarts=1 (arm and x86) clocksource=pit (x86
    // only) but the set takes care of that. for 16k image, there is extra kernel_cmdline.txt
    {
        std::ifstream cmdline_file(avd.GetSystemImagePaths().kernel_cmdline);
        std::string first_line;
        if (cmdline_file.is_open()) {
            if (std::getline(cmdline_file, first_line)) {
                cl.merge(absl::btree_set<std::string>(
                        absl::StrSplit(first_line, ' ', absl::SkipEmpty())));
            }
        }
    }

    for (auto* a = opts.append; a != nullptr; a = a->next) {
        cl.insert(a->param);
    }

    return absl::StrJoin(cl, " ");
}
}  // namespace

absl::Status KernelDevice::initialize(const EmulatorConfig& emulator) {
    const Avd& avd = emulator.avd();
    const AndroidOptions& opts = emulator.opts();
    mDiskImage = avd.GetSystemImagePaths().kernel_image.string();
    ASSIGN_OR_RETURN(auto cl, command_line(avd, opts));
    mCommandLine = std::move(cl);
    return absl::OkStatus();
}

// TODO(jansene) add kernel versioning magic to add/subtract parameters,
std::vector<std::string> KernelDevice::getQemuParameters(const EmulatorConfig& emulator) const {
    return {"-kernel", mDiskImage, "-append", mCommandLine};
}

}  // namespace android::goldfish
