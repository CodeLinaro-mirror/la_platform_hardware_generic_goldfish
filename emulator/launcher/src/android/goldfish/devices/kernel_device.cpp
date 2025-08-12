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

#include <android/cmdline-definitions.h>

#include <initializer_list>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include "aemu/base/utils/status_macros.h"
#include "android/base/system/System.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/emulator.h"
#include "android/goldfish/config/hardware_config.h"
#include "android/goldfish/devices/device.h"

namespace android::goldfish {

namespace {
absl::StatusOr<fs::path> kernel_image(const Avd& avd, const AndroidOptions& opts) {
    // Use the one provided by flag if present.
    if (opts.kernel != nullptr) {
        return opts.kernel;
    }

    // Use the one provided by hardware config if available
    if (const auto& hw = avd.hw(); !hw.kernel_path.empty()) {
        return hw.kernel_path;
    }

    // Get the one defined in the avd.
    auto options = {Avd::ImageType::KERNELRANCHU, Avd::ImageType::KERNELRANCHU64, Avd::ImageType::KERNEL};
    for (const auto& option : options) {
        auto kernel_image = avd.getSystemImageFilePath(option);
        if (kernel_image.ok()) {
            // TODO Also update hw.kernel_path with the found image?
            return *kernel_image;
        }
        LOG(INFO) << kernel_image.status().message();
    }

    return absl::NotFoundError("No kernel image found");
}

absl::StatusOr<std::string> command_line(const Avd& avd, const AndroidOptions& opts) {
    // Note the parameters need to be within '
    std::string cl = "'no_timer_check 8250.nr_uarts=1 loop.max_part=7";

    switch (auto a = avd.detectArchitecture(); a) {
        case Avd::CpuArchitecture::kArm:
            absl::StrAppend(&cl, absl::StrJoin({" console=ttyAMA0,38400", "keep_bootcon",
                                                "earlyprintk=ttyAMA0", "ndns=3"},
                                               " "));
            break;
        case Avd::CpuArchitecture::kX86:
            absl::StrAppend(&cl,
                            " clocksource=pit console=0 cma=296M@0-4G "
                            "memmap=0x10000$0xff018000");
            break;
        case Avd::CpuArchitecture::kRiscV:
        default:
            return absl::UnimplementedError(absl::StrCat("Machine type not supported: ", a));
    }

    if (opts.shell || opts.shell_serial || opts.show_kernel) {
        absl::StrAppend(&cl, " printk.devkmsg=on");
    }
    absl::StrAppend(&cl, " bootconfig");

    for (auto *a = opts.append; a != nullptr; a = a->next) {
        absl::StrAppend(&cl, " ", a->param);
    }

    absl::StrAppend(&cl, "'");
    return cl;
}
}  // namespace

absl::Status KernelDevice::initialize(const Emulator& emulator) {
    const Avd& avd = emulator.avd();
    const AndroidOptions& opts = emulator.opts();
    ASSIGN_OR_RETURN(auto k, kernel_image(avd, opts));
    mDiskImage = android::base::System::pathAsString(k);
    ASSIGN_OR_RETURN(auto cl, command_line(avd, opts));
    mCommandLine = std::move(cl);
    return absl::OkStatus();
}

// TODO(jansene) add kernel versioning magic to add/subtract parameters,
std::vector<std::string> KernelDevice::getQemuParameters(const Emulator& emulator) const {
    return {"-kernel", mDiskImage, "-append", mCommandLine};
}

}  // namespace android::goldfish