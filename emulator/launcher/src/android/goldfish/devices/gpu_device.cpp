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
#include <initializer_list>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/hardware_config.h"
#include "android/goldfish/devices/device.h"
#include "gpu_device.h"

namespace android::goldfish {

absl::Status GpuDevice::initialize(const Emulator& emulator) {
    return absl::OkStatus();
}

std::vector<std::string> GpuDevice::getQemuParameters(const Emulator& emulator) const {
    const auto& hw = emulator.avd().hw();
    return {"-device",
            absl::StrJoin({"virtio-gpu-rutabaga", "x-gfxstream-gles=on", "gfxstream-vulkan=on",
                           "x-gfxstream-composer=on", "hostmem=256M", absl::StrCat("id=", mGpuName),
                           absl::StrCat("xres=", hw.hw_lcd_width),
                           absl::StrCat("yres=", hw.hw_lcd_height)},
                          ",")};
}

}  // namespace android::goldfish