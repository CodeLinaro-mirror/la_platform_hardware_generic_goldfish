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

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include "android/base/system.h"
#include "android/goldfish/avd.h"
#include "android/goldfish/constants.h"
#include "android/goldfish/hardware_config.h"
#include "android/status/status_macros.h"
#include "goldfish/file/file.h"
#include "goldfish/parsing/resizable_display_config.h"
#include "goldfish/snapshot/snapshot_store.h"

namespace android::goldfish {
namespace {

using android::base::System;

}  // namespace

absl::Status GpuDevice::initialize(const EmulatorConfig& emulator) {
    LOG(INFO) << "Using virtio-gpu device: virtio-gpu-gfxstream";

    const ::goldfish::gpu::GpuSelection& selection = emulator.gpu_selection();
    LOG(INFO) << "Selected GPU mode: " << selection.chosen_gpu_mode;
    LOG(INFO) << "Using Vulkan ICD: "
              << (selection.vulkan_icd.empty() ? "System Default" : selection.vulkan_icd);
    mEmuRenderer = selection.emu_renderer;
    mVulkanIcd = selection.vulkan_icd;
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

    if (std::string select_gpu = System::Get()->EnvGet("ANDROID_EMU_VK_SELECT_GPU");
        !select_gpu.empty()) {
        env["ANDROID_EMU_VK_SELECT_GPU"] = std::move(select_gpu);
    }

#ifdef __linux__
    // The bundled Mesa libGLESv2.so/libEGL.so (used when the renderer needs
    // GLES, e.g. API < 37 guests) pull in lib64/libgallium_dri.so, which has a
    // ~4.4 KiB initial-exec TLS segment. glibc only reserves 512 bytes of
    // static TLS for dlopen()ed libraries, so the dlopen fails with "cannot
    // allocate memory in static TLS block" and gfxstream falls back to the
    // host's libGLESv2.so.2 (absent on minimal machines: crash). Reserve more.
    constexpr std::string_view kStaticTlsTunable = "glibc.rtld.optional_static_tls=16384";
    if (std::string tunables = System::Get()->EnvGet("GLIBC_TUNABLES"); tunables.empty()) {
        env["GLIBC_TUNABLES"] = std::string(kStaticTlsTunable);
    } else if (!absl::StrContains(tunables, "glibc.rtld.optional_static_tls")) {
        env["GLIBC_TUNABLES"] = absl::StrCat(tunables, ":", kStaticTlsTunable);
    } else {
        env["GLIBC_TUNABLES"] = std::move(tunables);
    }

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
    using S = android_studio::EmulatorFeatureFlagState;
    const auto& hw = emulator.avd().Hw();
    const AndroidOptions& opts = emulator.opts();
    const auto& ff = emulator.feature_flags();
    const int api_level = emulator.avd().ApiLevel();
    const bool legacy_goldfish_gpu = !UseVirtioGpu(api_level, ff.IsEnabled(S::MINIGBM));
    const bool vulkan_enabled = !legacy_goldfish_gpu || api_level >= 31;

    std::vector<std::string> renderer_features;

    bool needs_gles = true;
    if (!legacy_goldfish_gpu && !opts.no_guest_angle && ff.IsEnabled(S::GUEST_ANGLE)) {
        renderer_features.push_back("GuestVulkanOnly:enabled");

        // VulkanNativeSwapchain composition should only be enabled with GuestVulkanOnly
        if (!opts.no_vulkan_composition) {
            renderer_features.push_back("VulkanNativeSwapchain:enabled");
            needs_gles = false;
        }
    }

    // Use skiavkthreaded by default on virtio-gpu, skiaglthreaded on legacy goldfish_pipe.
    std::string systemui_renderer =
            opts.systemui_renderer ? opts.systemui_renderer
                                   : (legacy_goldfish_gpu ? "skiaglthreaded" : "skiavkthreaded");

    // Enable virtual queue for non-skiagl renderers, or when explicitly requested for skiagl.
    if (!legacy_goldfish_gpu && ff.IsEnabled(S::VULKAN_VIRTUAL_QUEUE) &&
        (!systemui_renderer.starts_with("skiagl") || ff.IsOverridden(S::VULKAN_VIRTUAL_QUEUE))) {
        renderer_features.push_back("VulkanVirtualQueue:enabled");
    }

    // Snapshots can also be taken at runtime (gRPC, console), not only at exit.
    // VulkanSnapshots is only enabled on virtio-gpu-gfxstream; legacy
    // goldfish_pipe keeps VulkanSnapshots disabled.
    if (!legacy_goldfish_gpu && mSnapshotDevice.snapshots_enabled() &&
        ff.IsEnabled(S::VULKAN_SNAPSHOTS)) {
        renderer_features.push_back("VulkanSnapshots:enabled");
    }

    renderer_features.push_back(ff.IsEnabled(S::VULKAN_BATCHED_DESCRIPTOR_SET_UPDATE)
                                        ? "VulkanBatchedDescriptorSetUpdate:enabled"
                                        : "VulkanBatchedDescriptorSetUpdate:disabled");
    renderer_features.push_back(vulkan_enabled ? "Vulkan:enabled" : "Vulkan:disabled");

    if (legacy_goldfish_gpu) {
        renderer_features.push_back("GlAsyncSwap:enabled");
        renderer_features.push_back((api_level >= 29) ? "GlDirectMem:enabled"
                                                      : "GlDirectMem:disabled");
        renderer_features.push_back((api_level >= 30)
                                            ? "HasSharedSlotsHostMemoryAllocator:enabled"
                                            : "HasSharedSlotsHostMemoryAllocator:disabled");
        renderer_features.push_back((api_level >= 28) ? "RefCountPipe:enabled"
                                                      : "RefCountPipe:disabled");
        renderer_features.push_back("YuvCache:enabled");
        renderer_features.push_back("HostComposition:enabled");
    } else if (ff.IsEnabled(S::VULKAN_USE_DEDICATED_AHB_MEMORY_TYPE)) {
        // Use a separate memory type for Ahb allocations, to avoid marking them
        // as host visible and coherent on lavapipe, which is not very well supported
        // in virtio-gpu path right now.
        renderer_features.push_back("VulkanUseDedicatedAhbMemoryType:enabled");
    }

    constexpr std::pair<EmulatorFeatureFlag, std::string_view> kOptionalRendererFeatures[] = {
        {S::BYPASS_VULKAN_DEVICE_FEATURE_OVERRIDES, "BypassVulkanDeviceFeatureOverrides"},
        {S::VULKAN_ALLOCATE_DEVICE_MEMORY_ONLY, "VulkanAllocateDeviceMemoryOnly"},
        {S::VULKAN_ALLOCATE_HOST_MEMORY, "VulkanAllocateHostMemory"},
        {S::VULKAN_DEBUG_UTILS, "VulkanDebugUtils"},
        {S::VULKAN_COMMAND_BUFFER_CHECKPOINTS, "VulkanCommandBufferCheckpoints"},
        {S::VULKAN_ROBUSTNESS, "VulkanRobustness"},
        {S::VULKAN_PROTECTED_MEMORY_EMULATION, "VulkanProtectedMemoryEmulation"},
    };
    for (const auto& [flag, name] : kOptionalRendererFeatures) {
        if (ff.IsEnabled(flag)) {
            renderer_features.push_back(absl::StrCat(name, ":enabled"));
        }
    }

    // Limit available GPU memory and hostmem to 4GB
    renderer_features.push_back("VulkanMaxSafeHeapSize:4294967296");

    // Temporarily limit guest to Vulkan 1.3, unless 1.4 is explicitly enabled via an env variable.
    std::string env_vk_enable_1_4 = System::Get()->EnvGet("ANDROID_EMU_VK_ENABLE_1_4");
    if (env_vk_enable_1_4.empty() || env_vk_enable_1_4[0] == '0') {
        renderer_features.push_back("GuestVulkanMaxApiVersion:1.3.0");
    }

    if (opts.vulkan_validation) {
        renderer_features.push_back(absl::StrCat("VulkanValidation:", opts.vulkan_validation));
    }
    if (opts.vulkan_validation_include_filter) {
        renderer_features.push_back(absl::StrCat("VulkanValidationIncludeFilter:",
                                                 opts.vulkan_validation_include_filter));
    }
    if (opts.vulkan_validation_exclude_filter) {
        renderer_features.push_back(absl::StrCat("VulkanValidationExcludeFilter:",
                                                 opts.vulkan_validation_exclude_filter));
    }

    // Append user-supplied -renderer-features last so CLI overrides take
    // precedence over the built-in defaults above.
    if (opts.renderer_features && *opts.renderer_features != '\0') {
        renderer_features.emplace_back(opts.renderer_features);
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

    const std::string joined_features = absl::StrJoin(renderer_features, ";");

    // The GPU device keeps the renderer state of each snapshot under
    // <snapshot_directory>/<snapshot name>/renderer/, next to the version.txt
    // that SnapshotStore writes to <snapshot_directory>/<snapshot name>/.
    std::optional<fs::path> snapshot_directory;
    if (mSnapshotDevice.snapshots_enabled()) {
        snapshot_directory =
                ::goldfish::snapshot::FileSnapshotStore(emulator.avd().GetContentPath(),
                                                        kSnapshotVersion)
                        .Root() /
                "";
    }

    if (legacy_goldfish_gpu) {
        std::vector<std::string> args;
        const auto global = [&args](std::string_view property, std::string_view value) {
            args.push_back("-global");
            args.push_back(absl::StrCat("driver=goldfish_pipe,property=", property,
                                        ",value=", EscapeQemuOptionValue(value)));
        };
        global("xres", absl::StrCat(width));
        global("yres", absl::StrCat(height));
        // The gfxstream RenderLib needs the guest's API level (setGuestAndroidApiLevel);
        // goldfish_pipe refuses to start graphics without it.
        global("guest_api_level", absl::StrCat(emulator.avd().ApiLevel()));
        global("renderer_features", joined_features);
        if (snapshot_directory) {
            global("snapshot_directory", snapshot_directory->string());
        }
        return args;
    }

    std::string gfxstream_backends = "gfxstream-vulkan=on";
    if (needs_gles) {
        gfxstream_backends.append(",x-gfxstream-gles=on");
    }

    std::vector<std::string> params = {"virtio-gpu-gfxstream"};
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
    params.push_back(absl::StrCat("renderer_features=", EscapeQemuOptionValue(joined_features)));
    if (snapshot_directory) {
        params.push_back(absl::StrCat("snapshot_directory=",
                                      EscapeQemuOptionValue(snapshot_directory->string())));
    }

    params.push_back(absl::StrCat("xres=", width));
    params.push_back(absl::StrCat("yres=", height));

    return {"-device", absl::StrJoin(params, ",")};
}

}  // namespace android::goldfish
