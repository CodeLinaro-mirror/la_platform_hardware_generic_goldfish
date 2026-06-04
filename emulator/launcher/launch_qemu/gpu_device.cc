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
#include "absl/strings/str_split.h"

#include "android/goldfish/avd.h"
#include "android/goldfish/hardware_config.h"
#include "goldfish/sensors/foldable_model.h"

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

    if (opts.skiavk) {
        renderer_features.append(";VulkanVirtualQueue:enabled");
    }

    if (mSnapshotEnabled) {
        renderer_features.append(";VulkanSnapshots:enabled");
    }

    renderer_features.append(";VulkanBatchedDescriptorSetUpdate:disabled");

    // Temporarily limit guest to Vulkan 1.3, unless 1.4 is explicitly enabled via an env variable.
    const char* env_vk_enable_1_4 = getenv("ANDROID_EMU_VK_ENABLE_1_4");
    if (!env_vk_enable_1_4 || env_vk_enable_1_4[0] == '0') {
        renderer_features.append(";GuestVulkanMaxApiVersion:1.3.0");
    }

    std::string gfxstream_backends = "gfxstream-vulkan=on";
    if (needs_gles) {
        gfxstream_backends.append(",x-gfxstream-gles=on");
    }

    std::vector<std::string> params = {"virtio-gpu-rutabaga"};
    if (hw.hw_sensor_hinge) {
        params.push_back("edid=off");
        params.push_back("max_outputs=2");
    }

    params.push_back(absl::StrCat("id=", mGpuName));
    params.push_back("hostmem=256M");
    params.push_back(gfxstream_backends);
    params.push_back("x-gfxstream-composer=on");
    params.push_back(absl::StrCat("renderer_features=", renderer_features));
    if (mSnapshotEnabled) {
        auto snapshot_directory = emulator.avd().GetContentPath() / "snapshots" / "renderer" / "";
        params.push_back(absl::StrCat("snapshot_directory=", snapshot_directory.string()));
    }
    auto resizable_configs =
            ::goldfish::sensors::FoldableModel::ParseResizableConfigs(hw.hw_resizable_configs);
    if (!resizable_configs.empty()) {
        params.push_back(absl::StrCat("xres=", resizable_configs[0].width));
        params.push_back(absl::StrCat("yres=", resizable_configs[0].height));
    } else {
        params.push_back(absl::StrCat("xres=", hw.hw_lcd_width));
        params.push_back(absl::StrCat("yres=", hw.hw_lcd_height));
    }

    return {"-device", absl::StrJoin(params, ",")};
}

}  // namespace android::goldfish
