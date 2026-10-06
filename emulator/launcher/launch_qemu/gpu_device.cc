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

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include "android/base/system.h"
#include "android/goldfish/avd.h"
#include "android/goldfish/hardware_config.h"
#include "goldfish/parsing/resizable_display_config.h"

namespace android::goldfish {
namespace {

using android::base::System;

struct GpuSelection {
    std::string chosen_gpu_mode;
    std::string emu_renderer;
    std::string vulkan_icd;
};

bool isHostGpuDenyListed() {
    // TODO(b/520445586) Check server side gpu list and determine if host is denylisted for GPU
    // Using software rendering by default for now.
    return true;
}

GpuSelection selectGpu(const EmulatorConfig& emulator) {
    const auto& o = emulator.opts();
    const auto& a = emulator.avd();

    // Check command line and AVD options, use host by default
    const std::string user_gpu_mode = o.gpu ? std::string(o.gpu) : "";
    const std::string avd_gpu_mode = a.Hw().hw_gpu_mode;
    const std::string chosen_gpu_mode =
            (!user_gpu_mode.empty() ? user_gpu_mode
                                    : (!avd_gpu_mode.empty() ? avd_gpu_mode : "auto"));

    const bool use_host_gpu =
            (chosen_gpu_mode == "host") || (chosen_gpu_mode == "auto" && !isHostGpuDenyListed());

    std::string chosen_emu_renderer;
    std::string chosen_vulkan_icd;
    if (use_host_gpu) {
        chosen_emu_renderer = "host";
#if defined(__APPLE__)
        // Use KosmicKrisp as the default Vulkan ICD on macOS, allow changing to MoltenVK with an
        // environment variable
        if (System::Get()->EnvGet("ANDROID_EMU_VK_SELECT_ICD") == "moltenvk") {
            chosen_vulkan_icd = "moltenvk";
        } else {
            chosen_vulkan_icd = "kosmickrisp";
        }
#else
        // Use system defaults
        chosen_vulkan_icd = "";
#endif
    } else {
        // Use lavapipe for software or invalid modes
        chosen_emu_renderer = "lavapipe";
        chosen_vulkan_icd = "lavapipe";
    }

    return {
        .chosen_gpu_mode = chosen_gpu_mode,
        .emu_renderer = chosen_emu_renderer,
        .vulkan_icd = chosen_vulkan_icd,
    };
}

}  // namespace

absl::Status GpuDevice::initialize(const EmulatorConfig& emulator) {
    auto selection = selectGpu(emulator);
    LOG(INFO) << "Selected GPU mode: " << selection.chosen_gpu_mode;
    LOG(INFO) << "Using Vulkan ICD: "
              << (selection.vulkan_icd.empty() ? "System Default" : selection.vulkan_icd);
    mEmuRenderer = std::move(selection.emu_renderer);
    mVulkanIcd = std::move(selection.vulkan_icd);
    return absl::OkStatus();
}

absl::flat_hash_map<std::string, std::string> GpuDevice::getQemuEnv(
        const EmulatorConfig& emulator) const {
    // Note: Gfxstream also uses ANDROID_EMULATOR_LIBRARY_DIR to locate GL and
    // Vulkan libraries, which is set in PluginInit::getQemuEnv.
    absl::flat_hash_map<std::string, std::string> env = {
        {"ANDROID_EMU_RENDERER", mEmuRenderer},
        {"ANDROID_EMU_VK_ICD", mVulkanIcd},
        {"ANDROID_EGL_ON_EGL", "1"},
        {"ANGLE_DEFAULT_PLATFORM", "vulkan"},
        {"ANDROID_EMULATOR_LAUNCHER_DIR", emulator.emulator_paths().launcher_directory.string()},
    };

#ifdef __linux__
    // Bug: 417138854: work around the log spam "bad fde: FDE is really a CIE"
    // when using the host system's libvulkan_lvp.so.
    if (System::Get()->EnvGet("ANDROID_EMU_PRELOAD_LIBGCC") == "1") {
        std::string current_preload = System::Get()->EnvGet("LD_PRELOAD");
        std::string libgcc_path = "/lib/x86_64-linux-gnu/libgcc_s.so.1";
        if (current_preload.empty()) {
            env["LD_PRELOAD"] = std::move(libgcc_path);
        } else {
            env["LD_PRELOAD"] = absl::StrCat(libgcc_path, ":", current_preload);
        }
    }
#endif

    // TODO: Decide which of these extra graphics / Gfxstream / VirtualScene /
    // VideoBridge environment variables should be passed through or replaced
    // with explicit parameters:
    //
    // constexpr std::string_view kPassthroughGpuVars[] = {
    //     // Linux display server / WSI
    //     "DISPLAY",
    //     "WAYLAND_DISPLAY",
    //     "XAUTHORITY",
    //
    //     // Vulkan Loader & Layers
    //     "VK_ICD_FILENAMES",
    //     "VK_DRIVER_FILES",
    //     "VK_ADD_LAYER_PATH",
    //     "VK_LAYER_PATH",
    //     "VK_INSTANCE_LAYERS",
    //     "VK_LOADER_LAYERS_DISABLE",
    //     "VK_LOADER_DEBUG",
    //     "ANDROID_EMU_VK_SELECT_GPU",
    //     "ANDROID_EMU_VK_LOADER_PATH",
    //     "ANDROID_EMU_VK_NO_CLEANUP",
    //     "ANDROID_EMU_VK_DISABLE_DEFERRED_COMMANDS",
    //
    //     // Vulkan Validation Layers & RenderDoc (gfxstream)
    //     "ANDROID_EMU_VVL_BEHAVIOR",
    //     "ANDROID_EMU_VVL_INCLUDE_FILTER",
    //     "ANDROID_EMU_VVL_EXCLUDE_FILTER",
    //     "GFXSTREAM_USE_TESTING_VALIDATION_LAYERS",
    //     "ANDROID_EMU_RENDERDOC",
    //     "ANDROID_EMU_RENDERDOC_LIBRARY_PATH",
    //     "ANDROID_EMU_RENDERDOC_CAPTURE_PATH_TEMPLATE",
    //
    //     // Gfxstream Logging / Debug / Windowing
    //     "GFXSTREAM_LOG_LEVEL",
    //     "GFXSTREAM_LOG_VERBOSE",
    //     "ANDROID_EMUGL_VERBOSE",
    //     "ANDROID_EMU_VK_LOG_CALLS",
    //     "ANDROID_EMU_VIRTIO_DEBUG",
    //     "ANDROID_EMUGL_RENDERTHREAD_STATS",
    //     "SHOW_FPS_STATS",
    //     "ANDROID_GFXSTREAM_EGL",
    //     "ANDROID_GFXSTREAM_CAPTURE_VK_SNAPSHOT",
    //     "ANDROID_EMUGL_EXPERIMENTAL_FAST_PATH",
    //     "ANDROID_EMU_HEADLESS",
    //     "ANDROID_EMUGL_EGL_VALIDATION",
    //     "ANDROID_EMU_USE_METAL_BACKED_VIEWS",
    //
    //     // VirtualScene (ver) & VideoBridge (videobridge)
    //     "VER_RENDERER_BACKEND",
    //     "ANDROID_EMU_ENABLE_VIDEO360",
    //     "ANDROID_EMU_ENABLE_STREETVIEW",
    //     "ANDROID_EMU_STREETVIEW_IMAGE_PATH",
    //     "ANDROID_EMU_MAPS_KEY",
    //     "ANDROID_EMU_SAVE_STREETVIEW_IMAGES",
    //     "ANDROID_EMU_VIDEO_ENCODER",
    //     "NVENC_LIB_PATH",
    //     "CUDA_LIB_PATH",
    //     "LIBVA_PATH",
    //     "LIBVA_DRIVER_NAME",
    // };
    // auto* sys = System::Get();
    // for (std::string_view var : kPassthroughGpuVars) {
    //     if (std::string val = sys->EnvGet(var); !val.empty()) {
    //         env.emplace(var, std::move(val));
    //     }
    // }

    return env;
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

    // Use skiavkthreaded by default, unless user specifies otherwise
    std::string systemui_renderer =
            opts.systemui_renderer ? opts.systemui_renderer : "skiavkthreaded";

    // Enable virtual queue with skiavk variants
    if (systemui_renderer.starts_with("skiavk")) {
        renderer_features.append(";VulkanVirtualQueue:enabled");
    }

    if (mSnapshotDevice.is_enabled()) {
        renderer_features.append(";VulkanSnapshots:enabled");
    }

    renderer_features.append(";VulkanBatchedDescriptorSetUpdate:disabled");

    // Use a separate memory type for Ahb allocations, to avoid marking them
    // as host visible and coherent on lavapipe, which is not very well supported
    // in virtio-gpu path right now.
    renderer_features.append(";VulkanUseDedicatedAhbMemoryType:enabled");

    // Limit available GPU memory and hostmem to 4GB
    renderer_features.append(";VulkanMaxSafeHeapSize:4294967296");

    // Temporarily limit guest to Vulkan 1.3, unless 1.4 is explicitly enabled via an env variable.
    std::string env_vk_enable_1_4 = System::Get()->EnvGet("ANDROID_EMU_VK_ENABLE_1_4");
    if (env_vk_enable_1_4.empty() || env_vk_enable_1_4[0] == '0') {
        renderer_features.append(";GuestVulkanMaxApiVersion:1.3.0");
    }

    if (opts.vulkan_validation) {
        renderer_features.append(absl::StrCat(";VulkanValidation:", opts.vulkan_validation));
    }
    if (opts.vulkan_validation_include_filter) {
        renderer_features.append(absl::StrCat(";VulkanValidationIncludeFilter:",
                                              opts.vulkan_validation_include_filter));
    }
    if (opts.vulkan_validation_exclude_filter) {
        renderer_features.append(absl::StrCat(";VulkanValidationExcludeFilter:",
                                              opts.vulkan_validation_exclude_filter));
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
    if (opts.no_window) {
        params.push_back("wsi=headless");
    }

    params.push_back(absl::StrCat("id=", id()));
    params.push_back("hostmem=4096M");
    params.push_back(gfxstream_backends);
    params.push_back("x-gfxstream-composer=on");
    params.push_back(absl::StrCat("renderer_features=", renderer_features));
    if (mSnapshotDevice.is_enabled()) {
        auto snapshot_directory = emulator.avd().GetContentPath() / "renderersave" / "";
        params.push_back(absl::StrCat("snapshot_directory=", snapshot_directory.string()));
    }

    size_t width;
    size_t height;
    if (hw.hw_resizable_configs.empty()) {
        width = hw.hw_lcd_width;
        height = hw.hw_lcd_height;
    } else {
        const auto resizable_configs =
                ::goldfish::parsing::ParseResizableDisplayConfig(hw.hw_resizable_configs);

        if (!resizable_configs || resizable_configs->empty()) {
            LOG(ERROR) << "Failed to parse hw_resizable_configs; GPU will be disabled. "
                          "config='"
                       << hw.hw_resizable_configs << "'";
            return {};
        }

        const auto& default_config = resizable_configs->front();
        width = default_config.width;
        height = default_config.height;
    }

    if (!width || !height) {
        LOG(ERROR) << "Invalid display dimensions; GPU will be disabled. "
                      "width="
                   << width << " height=" << height;
        return {};
    }

    params.push_back(absl::StrCat("xres=", width));
    params.push_back(absl::StrCat("yres=", height));

    return {"-device", absl::StrJoin(params, ",")};
}

}  // namespace android::goldfish
