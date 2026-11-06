// Copyright 2026 The Android Open Source Project
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

#include "goldfish/legacy/goldfish_gpu/renderer/renderer_config.h"

#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace goldfish::legacy {

SelectedRenderer ParseRendererBackend(std::string_view value) {
    SelectedRenderer selected = SELECTED_RENDERER_LAVAPIPE;
    if (value.empty()) {
        return selected;
    }
    if (value == "host" || value == "on") {
        LOG(WARNING) << "goldfish_gpu: ANDROID_EMU_RENDERER '" << value
                     << "' requests the host GPU, which is not available on the "
                        "goldfish_pipe path below API 37 yet; using lavapipe";
        selected = SELECTED_RENDERER_LAVAPIPE;
    } else if (value == "swiftshader" || value == "swiftshader_indirect") {
        LOG(WARNING) << "goldfish_gpu: ANDROID_EMU_RENDERER '" << value
                     << "' is deprecated (SwiftShader is being removed); using lavapipe";
        selected = SELECTED_RENDERER_LAVAPIPE;
    } else if (value == "lavapipe" || value == "vulkan") {
        selected = SELECTED_RENDERER_LAVAPIPE;
    } else if (value == "swangle" || value == "swangle_indirect" || value == "angle" ||
               value == "angle_indirect") {
        selected = SELECTED_RENDERER_ANGLE_INDIRECT;
    } else if (value == "off") {
        selected = SELECTED_RENDERER_OFF_DEPRECATED;
    } else {
        LOG(WARNING) << "goldfish_gpu: unknown ANDROID_EMU_RENDERER '" << value
                     << "', using lavapipe";
    }
    return selected;
}

absl::Status BuildFeatureSet(std::string_view renderer_features, bool egl_on_egl,
                             gfxstream::host::FeatureSet* features, uint32_t guest_api_level) {
    const bool vulkan_supported = IsGoldfishGpuVulkanSupported(guest_api_level);
    features->EglOnEgl.setEnabled(egl_on_egl);
    features->GlesDynamicVersion.setEnabled(true);
    features->NoDelayCloseColorBuffer.setEnabled(true);
    features->Surfaceless.setEnabled(true);
    // API 31..36 (goldfish_pipe) runs host GLES emulation alongside host Vulkan
    // emulation (not GuestAngle), with HostComposition over renderControl.
    // API <= 30 guests lack VulkanQueueSubmitWithCommands and keep Vulkan off.
    // VulkanSnapshots remains disabled on goldfish_pipe (API <= 36).
    features->Vulkan.setEnabled(vulkan_supported);
    features->VulkanBatchedDescriptorSetUpdate.setEnabled(true);
    features->VulkanIgnoredHandles.setEnabled(true);
    features->VulkanNullOptionalStrings.setEnabled(true);
    features->VulkanQueueSubmitWithCommands.setEnabled(true);
    features->VulkanShaderFloat16Int8.setEnabled(true);
    features->VulkanEnsureCachedCoherentMemoryAvailable.setEnabled(true);
    features->VulkanAstcEmulation.setEnabled(true);
    features->VulkanEtc2Emulation.setEnabled(true);

    // GlDirectMem + HasSharedSlotsHostMemoryAllocator provide BAR 1 DMA via
    // goldfish_address_space. Keep GlDma/GlDma2 disabled because they advertise
    // ANDROID_EMU_dma_v1/v2 for the legacy /dev/goldfish_pipe DMA ioctl
    // (kPipeCmdDmaHostMap), which is not supported and causes API <= 34
    // GL2Encoder::s_glMapBufferRange/s_glUnmapBuffer to pass unmapped pipe
    // physical addresses to s_glUnmapBufferDMA.
    static constexpr const char* kGoldfishDefaultFeatures[] = {
        "RefCountPipe:enabled",           "HasSharedSlotsHostMemoryAllocator:enabled",
        "VirtioGpuNext:disabled",         "VirtioGpuNativeSync:disabled",
        "GlDirectMem:enabled",            "GlDma:disabled",
        "GlDma2:disabled",                "GlAsyncSwap:enabled",
        "HostComposition:enabled",        "VulkanSnapshots:disabled",
        "GuestVulkanOnly:disabled",       "VulkanNativeSwapchain:disabled",
    };
    for (const char* feature : kGoldfishDefaultFeatures) {
        if (!features->processFeatureString(feature, "goldfish_pipe default")) {
            return absl::InternalError(
                    absl::StrCat("invalid default feature string '", feature, "'"));
        }
    }

    for (std::string_view feature :
         absl::StrSplit(renderer_features, absl::ByAnyChar(",;"), absl::SkipEmpty())) {
        const std::string entry(feature);
        if (!features->processFeatureString(entry, "goldfish_pipe renderer_features")) {
            return absl::InvalidArgumentError(absl::StrCat(
                    "invalid renderer_features entry '", entry,
                    "' (expected '<FeatureName>:enabled' or '<FeatureName>:disabled')"));
        }
    }

    // API <= 36 guests decode GLES on the host over pipe:opengles / ASG and
    // compose via CompositorGl, and do not enable VulkanSnapshots on
    // goldfish_pipe, so VulkanSnapshots, GuestVulkanOnly (which disables
    // EmulationGl), and VulkanNativeSwapchain cannot be used on this transport.
    for (gfxstream::host::BoolFeatureInfo* pinned :
         {&features->VulkanSnapshots, &features->GuestVulkanOnly,
          &features->VulkanNativeSwapchain}) {
        if (pinned->enabled()) {
            LOG(WARNING) << "goldfish_gpu: renderer_features enabled '" << pinned->getName()
                         << "', which the goldfish_pipe transport does not support; forcing it off";
            pinned->setEnabled(false);
        }
    }

    if (!vulkan_supported && features->Vulkan.enabled()) {
        LOG(WARNING) << "goldfish_gpu: renderer_features enabled 'Vulkan' on guest API level "
                     << guest_api_level << " (supported on API " << kMinGoldfishGpuVulkanApiLevel
                     << ".." << kMaxGoldfishGpuVulkanApiLevel << " only); forcing 'Vulkan' off";
        features->Vulkan.setEnabled(false);
    }
    return absl::OkStatus();
}

std::string_view VerifyModeName(GoldfishGpuVerifyMode mode) {
    switch (mode) {
    case GoldfishGpuVerifyMode::kOff:
        return "OFF";
    case GoldfishGpuVerifyMode::kLog:
        return "LOG";
    case GoldfishGpuVerifyMode::kEnforce:
        return "ENFORCE";
    case GoldfishGpuVerifyMode::kStrict:
        return "STRICT";
    }
    return "UNKNOWN";
}

GoldfishGpuVerifyMode ParseVerifyMode(std::string_view raw, GoldfishGpuVerifyMode default_mode) {
    if (raw.empty()) {
        return default_mode;
    }
    const std::string val = absl::AsciiStrToLower(raw);
    if (val == "0" || val == "off" || val == "false" || val == "none") {
        return GoldfishGpuVerifyMode::kOff;
    }
    if (val == "1" || val == "log" || val == "warn") {
        return GoldfishGpuVerifyMode::kLog;
    }
    if (val == "2" || val == "enforce" || val == "error") {
        return GoldfishGpuVerifyMode::kEnforce;
    }
    if (val == "3" || val == "strict" || val == "abort" || val == "fatal") {
        return GoldfishGpuVerifyMode::kStrict;
    }
    LOG(WARNING) << "goldfish_gpu: unknown ANDROID_EMU_GOLDFISH_GPU_VERIFY '" << raw
                 << "', keeping mode " << VerifyModeName(default_mode);
    return default_mode;
}

}  // namespace goldfish::legacy
