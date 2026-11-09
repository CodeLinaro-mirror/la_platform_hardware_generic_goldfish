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
#include "cpu_device.h"
#include "goldfish/file/file.h"
#include "initrd_device.h"

namespace android::goldfish {

namespace {
absl::StatusOr<std::string> command_line(const EmulatorConfig& emulator,
                                         const CpuDevice& cpu_device) {
    const Avd& avd = emulator.avd();
    const AndroidOptions& opts = emulator.opts();
    const int api_level = avd.ApiLevel();
    const bool has_bootconfig = (api_level <= 0 || api_level >= 31);

    // btree to provide deterministic (sorted) order.
    absl::btree_set<std::string> cl = {"no_timer_check", "8250.nr_uarts=1", "loop.max_part=7"};
    if (has_bootconfig) {
        cl.insert("bootconfig");
        cl.insert("mac80211_hwsim.radios=0");
    } else if (api_level == 30) {
        cl.insert("mac80211_hwsim.radios=1");
    } else if (api_level >= 25) {
        cl.insert("mac80211_hwsim.radios=2");
        cl.insert("mac80211_hwsim.channels=2");
    }

    // TODO add ramoops args?
    switch (auto a = avd.Arch(); a) {
    case Avd::CpuArchitecture::kArm:
        cl.merge(absl::btree_set<std::string>{"console=ttyAMA0,38400", "ndns=3"});
        break;
    case Avd::CpuArchitecture::kX86:
        cl.merge(absl::btree_set<std::string>{"console=ttyS0,38400", "clocksource=pit",
                                              "memmap=0x10000$0xff018000"});
        if (api_level > 0 && api_level < 26) {
            cl.insert("no-kvmclock");
        }
        if (cpu_device.accelerator() == "kvm") {
            cl.insert("tsc=reliable");
        }
        break;
    case Avd::CpuArchitecture::kRiscV:
    default:
        return absl::UnimplementedError(absl::StrCat("Machine type not supported: ", a));
    }

    // Unless kernel output was requested, the console UART is attached to -serial null;
    // suppress earlyprintk and pass quiet to prevent synchronous per-character UART VM exits.
    const bool verbose_kernel = opts.shell || opts.shell_serial || opts.show_kernel;
    if (verbose_kernel) {
        cl.insert("printk.devkmsg=on");
        switch (avd.Arch()) {
        case Avd::CpuArchitecture::kArm:
            cl.merge(absl::btree_set<std::string>{"earlyprintk=ttyAMA0", "keep_bootcon"});
            break;
        case Avd::CpuArchitecture::kX86:
            cl.insert("earlyprintk=ttyS0");
            break;
        case Avd::CpuArchitecture::kRiscV:
        default:
            break;
        }
    } else {
        cl.insert("quiet");
    }

    // Note that this is currently duplicating: 8250.nr_uarts=1 (arm and x86) clocksource=pit (x86
    // only) but the set takes care of that. for 16k image, there is extra kernel_cmdline.txt
    if (const auto& cmdline_path = avd.GetSystemImagePaths().kernel_cmdline;
        !cmdline_path.empty()) {
        std::ifstream cmdline_file(cmdline_path);
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

    // b/582851647: Prepend the default first; kernel module_blacklist is last-wins so any user
    // -append or kernel_cmdline.txt value overrides it.
    std::string result = absl::StrCat("module_blacklist=test_meminit ", absl::StrJoin(cl, " "));
    if (!has_bootconfig) {
        for (const auto& [key, val] : getBootProperties(emulator, /*vk_icd=*/"")) {
            if (val.empty()) {
                absl::StrAppend(&result, " ", key);
            } else {
                absl::StrAppend(&result, " ", key, "=", val);
            }
        }
    }
    return result;
}
}  // namespace

absl::Status KernelDevice::initialize(const EmulatorConfig& emulator) {
    const Avd& avd = emulator.avd();
    mDiskImage = avd.GetSystemImagePaths().kernel_image.string();
    ASSIGN_OR_RETURN(auto cl, command_line(emulator, mCpuDevice));
    mCommandLine = std::move(cl);
    return absl::OkStatus();
}

// TODO(jansene) add kernel versioning magic to add/subtract parameters,
std::vector<std::string> KernelDevice::getQemuParameters(const EmulatorConfig& emulator) const {
    return {"-kernel", mDiskImage, "-append", mCommandLine};
}

}  // namespace android::goldfish
