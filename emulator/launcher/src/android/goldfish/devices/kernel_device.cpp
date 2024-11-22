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
#include "android/goldfish/devices/kernel_device.h"

#include <initializer_list>
#include <string_view>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"

#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/emulator.h"
#include "android/goldfish/config/hardware_config.h"
#include "android/goldfish/devices/device.h"

namespace android::goldfish {
absl::Status KernelDevice::initialize(const Emulator& emulator) {
    const Avd& avd = emulator.avd();
    auto hw = avd.hw();

    // Use the one provided by hardware config if available
    if (!hw.kernel_path.empty()) {
        mDiskImage = hw.kernel_path;
        return absl::OkStatus();
    }

    // Get the one defined in the avd.
    auto options = {Avd::ImageType::KERNEL, Avd::ImageType::KERNELRANCHU64,
                    Avd::ImageType::KERNELRANCHU};
    for (const auto& option : options) {
        mDiskImage = avd.getSystemImagePath(option);
        if (mDiskImage.ok()) {
            hw.kernel_path = mDiskImage->string();
            return absl::OkStatus();
        }
        LOG(INFO) << mDiskImage.status().message();
    }

    return absl::NotFoundError("No kernel image found.");
}

// TODO(jansene) add kernel versioning magic to add/subtract parameters,
std::vector<std::string> KernelDevice::getQemuParameters(const Emulator& emulator) const {
    auto opts = emulator.opts();
    return {
            "-kernel",
            mDiskImage->string(),
            "-append",
            // Note the parameters need to be within '
            absl::StrFormat("'no_timer_check 8250.nr_uarts=1 clocksource=pit console=0 "
                            "cma=296M@0-4G loop.max_part=7 memmap=0x10000$0xff018000 "
                            "%s bootconfig'",
                            (opts.shell || opts.shell_serial || opts.show_kernel)
                                    ? "printk.devkmsg=on"
                                    : ""),
    };
}

}  // namespace android::goldfish