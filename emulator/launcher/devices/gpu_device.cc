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

#include "gpu_device.h"

#include <initializer_list>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include "android/goldfish/avd.h"
#include "android/goldfish/hardware_config.h"

namespace android::goldfish {

absl::Status GpuDevice::initialize(const EmulatorConfig& emulator) {
    return absl::OkStatus();
}

std::vector<std::string> GpuDevice::getQemuParameters(const EmulatorConfig& emulator) const {
    const auto& hw = emulator.avd().Hw();
    const AndroidOptions& opts = emulator.opts();

    std::string renderer_features = opts.renderer_features ? opts.renderer_features : "";
    bool needs_gles = true;
    if (!opts.no_guest_angle) {
        if (!renderer_features.empty()) {
            renderer_features.append(";");
        }
        renderer_features.append("GuestVulkanOnly:enabled");

        // VulkanNativeSwapchain composition should only be enabled with GuestVulkanOnly
        if (!opts.no_vulkan_composition) {
            renderer_features.append(";VulkanNativeSwapchain:enabled");
            needs_gles = false;
        }
    }

    std::string gfxstream_backends = "gfxstream-vulkan=on";
    if (needs_gles) {
        gfxstream_backends.append(",x-gfxstream-gles=on");
    }

    return {"-device", absl::StrJoin({"virtio-gpu-rutabaga", absl::StrCat("id=", mGpuName),
                                      "hostmem=256M", gfxstream_backends, "x-gfxstream-composer=on",
                                      absl::StrCat("renderer_features=", renderer_features),
                                      absl::StrCat("xres=", hw.hw_lcd_width),
                                      absl::StrCat("yres=", hw.hw_lcd_height)},
                                     ",")};
}

}  // namespace android::goldfish
