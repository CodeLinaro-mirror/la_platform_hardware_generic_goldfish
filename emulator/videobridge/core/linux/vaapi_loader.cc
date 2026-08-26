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

#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace goldfish::videobridge {

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

    const char* env_path = std::getenv("LIBVA_PATH");
    if (env_path && *env_path) {
        va_paths.push_back(env_path);
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
        "/dev/dri/renderD128",
        "/dev/dri/renderD129",
        "/dev/dri/card0",
    };

    for (const auto& va_path : va_paths) {
        goldfish::os::DynamicLibrary lib_va(va_path);
        if (!lib_va.ok()) continue;

        for (const auto& drm_path : drm_paths) {
            goldfish::os::DynamicLibrary lib_va_drm(drm_path);
            if (!lib_va_drm.ok()) continue;

            VaapiFunctionList fn_list{};
            fn_list.vaInitialize =
                    reinterpret_cast<decltype(fn_list.vaInitialize)>(lib_va["vaInitialize"]);
            fn_list.vaTerminate =
                    reinterpret_cast<decltype(fn_list.vaTerminate)>(lib_va["vaTerminate"]);
            fn_list.vaErrorStr =
                    reinterpret_cast<decltype(fn_list.vaErrorStr)>(lib_va["vaErrorStr"]);
            fn_list.vaMaxNumProfiles = reinterpret_cast<decltype(fn_list.vaMaxNumProfiles)>(
                    lib_va["vaMaxNumProfiles"]);
            fn_list.vaQueryConfigProfiles =
                    reinterpret_cast<decltype(fn_list.vaQueryConfigProfiles)>(
                            lib_va["vaQueryConfigProfiles"]);
            fn_list.vaMaxNumEntrypoints = reinterpret_cast<decltype(fn_list.vaMaxNumEntrypoints)>(
                    lib_va["vaMaxNumEntrypoints"]);
            fn_list.vaQueryConfigEntrypoints =
                    reinterpret_cast<decltype(fn_list.vaQueryConfigEntrypoints)>(
                            lib_va["vaQueryConfigEntrypoints"]);
            fn_list.vaGetConfigAttributes =
                    reinterpret_cast<decltype(fn_list.vaGetConfigAttributes)>(
                            lib_va["vaGetConfigAttributes"]);
            fn_list.vaCreateConfig =
                    reinterpret_cast<decltype(fn_list.vaCreateConfig)>(lib_va["vaCreateConfig"]);
            fn_list.vaDestroyConfig =
                    reinterpret_cast<decltype(fn_list.vaDestroyConfig)>(lib_va["vaDestroyConfig"]);
            fn_list.vaCreateContext =
                    reinterpret_cast<decltype(fn_list.vaCreateContext)>(lib_va["vaCreateContext"]);
            fn_list.vaDestroyContext = reinterpret_cast<decltype(fn_list.vaDestroyContext)>(
                    lib_va["vaDestroyContext"]);
            fn_list.vaCreateSurfaces = reinterpret_cast<decltype(fn_list.vaCreateSurfaces)>(
                    lib_va["vaCreateSurfaces"]);
            fn_list.vaDestroySurfaces = reinterpret_cast<decltype(fn_list.vaDestroySurfaces)>(
                    lib_va["vaDestroySurfaces"]);
            fn_list.vaCreateBuffer =
                    reinterpret_cast<decltype(fn_list.vaCreateBuffer)>(lib_va["vaCreateBuffer"]);
            fn_list.vaDestroyBuffer =
                    reinterpret_cast<decltype(fn_list.vaDestroyBuffer)>(lib_va["vaDestroyBuffer"]);
            fn_list.vaMapBuffer =
                    reinterpret_cast<decltype(fn_list.vaMapBuffer)>(lib_va["vaMapBuffer"]);
            fn_list.vaUnmapBuffer =
                    reinterpret_cast<decltype(fn_list.vaUnmapBuffer)>(lib_va["vaUnmapBuffer"]);
            fn_list.vaBeginPicture =
                    reinterpret_cast<decltype(fn_list.vaBeginPicture)>(lib_va["vaBeginPicture"]);
            fn_list.vaRenderPicture =
                    reinterpret_cast<decltype(fn_list.vaRenderPicture)>(lib_va["vaRenderPicture"]);
            fn_list.vaEndPicture =
                    reinterpret_cast<decltype(fn_list.vaEndPicture)>(lib_va["vaEndPicture"]);
            fn_list.vaSyncSurface =
                    reinterpret_cast<decltype(fn_list.vaSyncSurface)>(lib_va["vaSyncSurface"]);
            fn_list.vaDeriveImage =
                    reinterpret_cast<decltype(fn_list.vaDeriveImage)>(lib_va["vaDeriveImage"]);
            fn_list.vaDestroyImage =
                    reinterpret_cast<decltype(fn_list.vaDestroyImage)>(lib_va["vaDestroyImage"]);
            fn_list.vaGetDisplayDRM = reinterpret_cast<decltype(fn_list.vaGetDisplayDRM)>(
                    lib_va_drm["vaGetDisplayDRM"]);

            if (!fn_list.vaInitialize || !fn_list.vaTerminate || !fn_list.vaGetDisplayDRM ||
                !fn_list.vaCreateConfig || !fn_list.vaCreateContext || !fn_list.vaCreateSurfaces ||
                !fn_list.vaCreateBuffer || !fn_list.vaMapBuffer || !fn_list.vaUnmapBuffer ||
                !fn_list.vaBeginPicture || !fn_list.vaRenderPicture || !fn_list.vaEndPicture ||
                !fn_list.vaSyncSurface || !fn_list.vaDeriveImage || !fn_list.vaDestroyImage) {
                continue;
            }

            for (const auto& node : drm_nodes) {
                int fd = open(node.c_str(), O_RDWR | O_CLOEXEC);
                if (fd < 0) continue;

                VADisplay display = fn_list.vaGetDisplayDRM(fd);
                if (!display) {
                    close(fd);
                    continue;
                }

                int major = 0;
                int minor = 0;
                VAStatus status = fn_list.vaInitialize(display, &major, &minor);
                if (status != VA_STATUS_SUCCESS) {
                    close(fd);
                    continue;
                }

                return std::make_unique<VaapiLoader>(std::move(lib_va), std::move(lib_va_drm), fd,
                                                     display, fn_list);
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

VaapiLoader::~VaapiLoader() {
    if (display_ && fn_list_.vaTerminate) {
        fn_list_.vaTerminate(display_);
        display_ = nullptr;
    }
    if (drm_fd_ >= 0) {
        close(drm_fd_);
        drm_fd_ = -1;
    }
}

VaapiLoader::VaapiLoader(VaapiLoader&& other) noexcept
        : lib_va_(std::move(other.lib_va_))
        , lib_va_drm_(std::move(other.lib_va_drm_))
        , drm_fd_(other.drm_fd_)
        , display_(other.display_)
        , fn_list_(other.fn_list_) {
    other.drm_fd_ = -1;
    other.display_ = nullptr;
}

VaapiLoader& VaapiLoader::operator=(VaapiLoader&& other) noexcept {
    if (this != &other) {
        if (display_ && fn_list_.vaTerminate) {
            fn_list_.vaTerminate(display_);
        }
        if (drm_fd_ >= 0) {
            close(drm_fd_);
        }

        lib_va_ = std::move(other.lib_va_);
        lib_va_drm_ = std::move(other.lib_va_drm_);
        drm_fd_ = other.drm_fd_;
        display_ = other.display_;
        fn_list_ = other.fn_list_;

        other.drm_fd_ = -1;
        other.display_ = nullptr;
    }
    return *this;
}

}  // namespace goldfish::videobridge
