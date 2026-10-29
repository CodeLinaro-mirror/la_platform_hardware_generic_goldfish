// Copyright (C) 2026 The Android Open Source Project
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
#include "core/linux/vaapi_loader.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include "android/base/system.h"

namespace goldfish::videobridge {

namespace {

bool SupportsH264EncodeEntrypoint(const VaapiFunctionList& fn_list, VADisplay display) {
    if (!fn_list.vaMaxNumProfiles || !fn_list.vaQueryConfigProfiles ||
        !fn_list.vaMaxNumEntrypoints || !fn_list.vaQueryConfigEntrypoints) {
        return true;
    }

    int max_profiles = fn_list.vaMaxNumProfiles(display);
    if (max_profiles <= 0) max_profiles = 32;
    std::vector<VAProfile> profiles(static_cast<size_t>(max_profiles));
    int num_profiles = 0;
    if (fn_list.vaQueryConfigProfiles(display, profiles.data(), &num_profiles) !=
                VA_STATUS_SUCCESS ||
        num_profiles <= 0) {
        return false;
    }

    int max_entrypoints = fn_list.vaMaxNumEntrypoints(display);
    if (max_entrypoints <= 0) max_entrypoints = 16;
    std::vector<VAEntrypoint> entrypoints(static_cast<size_t>(max_entrypoints));

    int profile_count = std::min(num_profiles, max_profiles);
    for (int i = 0; i < profile_count; ++i) {
        VAProfile profile = profiles[i];
        if (profile != VAProfileH264ConstrainedBaseline && profile != VAProfileH264Baseline &&
            profile != VAProfileH264Main && profile != VAProfileH264High) {
            continue;
        }
        int num_entrypoints = 0;
        if (fn_list.vaQueryConfigEntrypoints(display, profile, entrypoints.data(),
                                             &num_entrypoints) != VA_STATUS_SUCCESS) {
            continue;
        }
        int entrypoint_count = std::min(num_entrypoints, max_entrypoints);
        for (int j = 0; j < entrypoint_count; ++j) {
            if (entrypoints[j] == VAEntrypointEncSlice ||
                entrypoints[j] == VAEntrypointEncSliceLP ||
                entrypoints[j] == VAEntrypointEncPicture) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

std::string VaapiStatusToString(VAStatus status) {
    switch (status) {
    case VA_STATUS_SUCCESS:
        return "VA_STATUS_SUCCESS (0: Operation succeeded)";
    case VA_STATUS_ERROR_OPERATION_FAILED:
        return "VA_STATUS_ERROR_OPERATION_FAILED (1: General driver operation failure)";
    case VA_STATUS_ERROR_ALLOCATION_FAILED:
        return "VA_STATUS_ERROR_ALLOCATION_FAILED (2: Out of system or GPU memory)";
    case VA_STATUS_ERROR_INVALID_DISPLAY:
        return "VA_STATUS_ERROR_INVALID_DISPLAY (3: Invalid VADisplay handle)";
    case VA_STATUS_ERROR_INVALID_CONFIG:
        return "VA_STATUS_ERROR_INVALID_CONFIG (4: Invalid VAConfigID handle)";
    case VA_STATUS_ERROR_INVALID_CONTEXT:
        return "VA_STATUS_ERROR_INVALID_CONTEXT (5: Invalid VAContextID handle)";
    case VA_STATUS_ERROR_INVALID_SURFACE:
        return "VA_STATUS_ERROR_INVALID_SURFACE (6: Invalid VASurfaceID handle)";
    case VA_STATUS_ERROR_INVALID_BUFFER:
        return "VA_STATUS_ERROR_INVALID_BUFFER (7: Invalid VABufferID handle)";
    case VA_STATUS_ERROR_INVALID_IMAGE:
        return "VA_STATUS_ERROR_INVALID_IMAGE (8: Invalid VAImageID handle)";
    case VA_STATUS_ERROR_INVALID_PARAMETER:
        return "VA_STATUS_ERROR_INVALID_PARAMETER (12: Invalid parameter passed to VA-API)";
    case VA_STATUS_ERROR_UNSUPPORTED_PROFILE:
        return "VA_STATUS_ERROR_UNSUPPORTED_PROFILE (13: Requested profile not supported by "
               "hardware ASIC)";
    case VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT:
        return "VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT (14: Requested entrypoint not supported)";
    case VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE:
        return "VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE (16: Buffer type not supported)";
    case VA_STATUS_ERROR_SURFACE_BUSY:
        return "VA_STATUS_ERROR_SURFACE_BUSY (18: Surface is currently in use by hardware)";
    case VA_STATUS_ERROR_UNIMPLEMENTED:
        return "VA_STATUS_ERROR_UNIMPLEMENTED (20: Feature not implemented in driver)";
    case VA_STATUS_ERROR_NOT_ENOUGH_BUFFER:
        return "VA_STATUS_ERROR_NOT_ENOUGH_BUFFER (32: Allocated buffer too small for coded "
               "bitstream)";
    case VA_STATUS_ERROR_UNKNOWN:
        return "VA_STATUS_ERROR_UNKNOWN: Unknown internal driver error";
    default:
        return absl::StrCat("VA_STATUS_UNKNOWN (", static_cast<int>(status), ")");
    }
}

absl::StatusOr<std::unique_ptr<VaapiLoader>> VaapiLoader::Create(
        const std::string& custom_library_path) {
    std::vector<std::string> va_paths;
    if (!custom_library_path.empty()) {
        va_paths.push_back(custom_library_path);
    }

    if (std::string env_path = android::base::System::Get()->EnvGet("LIBVA_PATH");
        !env_path.empty()) {
        va_paths.push_back(std::move(env_path));
    }

    va_paths.insert(va_paths.end(), {
                                        "libva.so.2",
                                        "libva.so",
                                        "/usr/lib/x86_64-linux-gnu/libva.so.2",
                                        "/usr/lib64/libva.so.2",
                                    });

    std::vector<std::string> drm_paths = {
        "libva-drm.so.2",
        "libva-drm.so",
        "/usr/lib/x86_64-linux-gnu/libva-drm.so.2",
        "/usr/lib64/libva-drm.so.2",
    };

    std::vector<std::string> drm_nodes = {
        "/dev/dri/renderD128", "/dev/dri/renderD129", "/dev/dri/renderD130",
        "/dev/dri/card0",      "/dev/dri/card1",
    };

    const std::string existing_driver_env =
            android::base::System::Get()->EnvGet("LIBVA_DRIVER_NAME");
    std::vector<const char*> candidate_drivers = {nullptr};
    if (existing_driver_env.empty()) {
        candidate_drivers.push_back("iHD");
        candidate_drivers.push_back("i965");
        candidate_drivers.push_back("radeonsi");
    }

    for (const auto& va_path : va_paths) {
        goldfish::os::DynamicLibrary lib_va(va_path);
        if (!lib_va.ok()) continue;

        for (const auto& drm_path : drm_paths) {
            goldfish::os::DynamicLibrary lib_va_drm(drm_path);
            if (!lib_va_drm.ok()) continue;

            VaapiFunctionList fn_list{};
            auto load_sym = [](goldfish::os::DynamicLibrary& lib, auto& fn, const char* name) {
                fn = reinterpret_cast<std::decay_t<decltype(fn)>>(lib[name]);
            };
            load_sym(lib_va, fn_list.vaInitialize, "vaInitialize");
            load_sym(lib_va, fn_list.vaTerminate, "vaTerminate");
            load_sym(lib_va, fn_list.vaErrorStr, "vaErrorStr");
            load_sym(lib_va, fn_list.vaMaxNumProfiles, "vaMaxNumProfiles");
            load_sym(lib_va, fn_list.vaQueryConfigProfiles, "vaQueryConfigProfiles");
            load_sym(lib_va, fn_list.vaMaxNumEntrypoints, "vaMaxNumEntrypoints");
            load_sym(lib_va, fn_list.vaQueryConfigEntrypoints, "vaQueryConfigEntrypoints");
            load_sym(lib_va, fn_list.vaGetConfigAttributes, "vaGetConfigAttributes");
            load_sym(lib_va, fn_list.vaCreateConfig, "vaCreateConfig");
            load_sym(lib_va, fn_list.vaDestroyConfig, "vaDestroyConfig");
            load_sym(lib_va, fn_list.vaCreateContext, "vaCreateContext");
            load_sym(lib_va, fn_list.vaDestroyContext, "vaDestroyContext");
            load_sym(lib_va, fn_list.vaCreateSurfaces, "vaCreateSurfaces");
            load_sym(lib_va, fn_list.vaDestroySurfaces, "vaDestroySurfaces");
            load_sym(lib_va, fn_list.vaCreateBuffer, "vaCreateBuffer");
            load_sym(lib_va, fn_list.vaDestroyBuffer, "vaDestroyBuffer");
            load_sym(lib_va, fn_list.vaMapBuffer, "vaMapBuffer");
            load_sym(lib_va, fn_list.vaUnmapBuffer, "vaUnmapBuffer");
            load_sym(lib_va, fn_list.vaBeginPicture, "vaBeginPicture");
            load_sym(lib_va, fn_list.vaRenderPicture, "vaRenderPicture");
            load_sym(lib_va, fn_list.vaEndPicture, "vaEndPicture");
            load_sym(lib_va, fn_list.vaSyncSurface, "vaSyncSurface");
            load_sym(lib_va, fn_list.vaDeriveImage, "vaDeriveImage");
            load_sym(lib_va, fn_list.vaDestroyImage, "vaDestroyImage");
            load_sym(lib_va_drm, fn_list.vaGetDisplayDRM, "vaGetDisplayDRM");

            if (!fn_list.vaInitialize || !fn_list.vaTerminate || !fn_list.vaGetDisplayDRM ||
                !fn_list.vaCreateConfig || !fn_list.vaCreateContext || !fn_list.vaCreateSurfaces ||
                !fn_list.vaCreateBuffer || !fn_list.vaMapBuffer || !fn_list.vaUnmapBuffer ||
                !fn_list.vaBeginPicture || !fn_list.vaRenderPicture || !fn_list.vaEndPicture ||
                !fn_list.vaSyncSurface || !fn_list.vaDeriveImage || !fn_list.vaDestroyImage) {
                continue;
            }

            for (const auto& node : drm_nodes) {
                for (const char* drv_name : candidate_drivers) {
                    if (drv_name) {
                        setenv("LIBVA_DRIVER_NAME", drv_name, 1);
                    } else if (existing_driver_env.empty()) {
                        unsetenv("LIBVA_DRIVER_NAME");
                    }

                    int fd = open(node.c_str(), O_RDWR | O_CLOEXEC);
                    if (fd < 0) break;

                    VADisplay display = fn_list.vaGetDisplayDRM(fd);
                    if (!display) {
                        close(fd);
                        continue;
                    }

                    int major = 0;
                    int minor = 0;
                    VAStatus status = fn_list.vaInitialize(display, &major, &minor);
                    if (status != VA_STATUS_SUCCESS ||
                        !SupportsH264EncodeEntrypoint(fn_list, display)) {
                        fn_list.vaTerminate(display);
                        close(fd);
                        continue;
                    }

                    return std::make_unique<VaapiLoader>(std::move(lib_va), std::move(lib_va_drm),
                                                         fd, display, fn_list);
                }
            }
            if (existing_driver_env.empty()) {
                unsetenv("LIBVA_DRIVER_NAME");
            }
        }
    }

    return absl::NotFoundError(absl::StrCat(
            "Could not locate or initialize Intel/AMD VA-API drivers. Checked libraries: [",
            absl::StrJoin(va_paths, ", "), "] and DRM nodes: [", absl::StrJoin(drm_nodes, ", "),
            "]. Remediation: Ensure Intel Media Driver (iHD) or Mesa VA-API drivers are installed "
            "and user has read/write permissions to /dev/dri/renderD128."));
}

absl::StatusOr<std::unique_ptr<VaapiLoader>> VaapiLoader::CreateForTest(
        const VaapiFunctionList& function_list, VADisplay display) {
    return std::make_unique<VaapiLoader>(function_list, display);
}

VaapiLoader::VaapiLoader(goldfish::os::DynamicLibrary lib_va,
                         goldfish::os::DynamicLibrary lib_va_drm, int drm_fd, VADisplay display,
                         const VaapiFunctionList& fn_list)
        : lib_va_(std::move(lib_va))
        , lib_va_drm_(std::move(lib_va_drm))
        , drm_fd_(drm_fd)
        , display_(display)
        , fn_list_(fn_list) {}

VaapiLoader::VaapiLoader(const VaapiFunctionList& fn_list, VADisplay display)
        : display_(display), fn_list_(fn_list) {}

VaapiLoader::~VaapiLoader() { Close(); }

VaapiLoader::VaapiLoader(VaapiLoader&& other) noexcept
        : lib_va_(std::move(other.lib_va_))
        , lib_va_drm_(std::move(other.lib_va_drm_))
        , drm_fd_(std::exchange(other.drm_fd_, -1))
        , display_(std::exchange(other.display_, nullptr))
        , fn_list_(other.fn_list_) {}

VaapiLoader& VaapiLoader::operator=(VaapiLoader&& other) noexcept {
    if (this != &other) {
        Close();
        lib_va_ = std::move(other.lib_va_);
        lib_va_drm_ = std::move(other.lib_va_drm_);
        drm_fd_ = std::exchange(other.drm_fd_, -1);
        display_ = std::exchange(other.display_, nullptr);
        fn_list_ = other.fn_list_;
    }
    return *this;
}

void VaapiLoader::Close() {
    if (display_ && fn_list_.vaTerminate) {
        fn_list_.vaTerminate(display_);
        display_ = nullptr;
    }
    if (drm_fd_ >= 0) {
        close(drm_fd_);
        drm_fd_ = -1;
    }
}

}  // namespace goldfish::videobridge
