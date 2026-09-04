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
#pragma once

#include <memory>
#include <string>

#include "absl/status/statusor.h"

#include "core/linux/va_api_types.h"
#include "goldfish/os/dynamic_library.h"

namespace goldfish::videobridge {

/**
 * @brief Converts a VA-API status code into a human- and AI-readable diagnostic string.
 */
std::string VaapiStatusToString(VAStatus status);

/**
 * @struct VaapiFunctionList
 * @brief Table of function pointers resolved from `libva.so.2` and `libva-drm.so.2`.
 */
struct VaapiFunctionList {
    VAStatus (*vaInitialize)(VADisplay dpy, int* major_version, int* minor_version){nullptr};
    VAStatus (*vaTerminate)(VADisplay dpy){nullptr};
    const char* (*vaErrorStr)(VAStatus error_status){nullptr};
    int (*vaMaxNumProfiles)(VADisplay dpy){nullptr};
    VAStatus (*vaQueryConfigProfiles)(VADisplay dpy, VAProfile* profile_list,
                                      int* num_profiles){nullptr};
    int (*vaMaxNumEntrypoints)(VADisplay dpy){nullptr};
    VAStatus (*vaQueryConfigEntrypoints)(VADisplay dpy, VAProfile profile,
                                         VAEntrypoint* entrypoint_list,
                                         int* num_entrypoints){nullptr};
    VAStatus (*vaGetConfigAttributes)(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint,
                                      VAConfigAttrib* attrib_list, int num_attribs){nullptr};
    VAStatus (*vaCreateConfig)(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint,
                               VAConfigAttrib* attrib_list, int num_attribs,
                               VAConfigID* config_id){nullptr};
    VAStatus (*vaDestroyConfig)(VADisplay dpy, VAConfigID config_id){nullptr};
    VAStatus (*vaCreateContext)(VADisplay dpy, VAConfigID config_id, int picture_width,
                                int picture_height, int flag, VASurfaceID* render_targets,
                                int num_render_targets, VAContextID* context){nullptr};
    VAStatus (*vaDestroyContext)(VADisplay dpy, VAContextID context){nullptr};
    VAStatus (*vaCreateSurfaces)(VADisplay dpy, unsigned int format, unsigned int width,
                                 unsigned int height, VASurfaceID* surfaces,
                                 unsigned int num_surfaces, VASurfaceAttrib* attrib_list,
                                 unsigned int num_attribs){nullptr};
    VAStatus (*vaDestroySurfaces)(VADisplay dpy, VASurfaceID* surfaces, int num_surfaces){nullptr};
    VAStatus (*vaCreateBuffer)(VADisplay dpy, VAContextID context, VABufferType type,
                               unsigned int size, unsigned int num_elements, void* data,
                               VABufferID* buf_id){nullptr};
    VAStatus (*vaDestroyBuffer)(VADisplay dpy, VABufferID buffer_id){nullptr};
    VAStatus (*vaMapBuffer)(VADisplay dpy, VABufferID buf_id, void** pbuf){nullptr};
    VAStatus (*vaUnmapBuffer)(VADisplay dpy, VABufferID buf_id){nullptr};
    VAStatus (*vaBeginPicture)(VADisplay dpy, VAContextID context,
                               VASurfaceID render_target){nullptr};
    VAStatus (*vaRenderPicture)(VADisplay dpy, VAContextID context, VABufferID* buffers,
                                int num_buffers){nullptr};
    VAStatus (*vaEndPicture)(VADisplay dpy, VAContextID context){nullptr};
    VAStatus (*vaSyncSurface)(VADisplay dpy, VASurfaceID render_target){nullptr};
    VAStatus (*vaDeriveImage)(VADisplay dpy, VASurfaceID surface, VAImage* image){nullptr};
    VAStatus (*vaDestroyImage)(VADisplay dpy, VAImageID image){nullptr};
    VADisplay (*vaGetDisplayDRM)(int fd){nullptr};
};

/**
 * @class VaapiLoader
 * @brief Dynamic loader and symbol resolver for Intel/AMD VA-API hardware acceleration library.
 *
 * Encapsulates dynamic loading of `libva.so.2` and `libva-drm.so.2`, opens the DRM render
 * device node, and initializes the `VADisplay` connection.
 */
class VaapiLoader {
  public:
    static absl::StatusOr<std::unique_ptr<VaapiLoader>> Create(
            const std::string& custom_library_path = "");

    static absl::StatusOr<std::unique_ptr<VaapiLoader>> CreateForTest(
            const VaapiFunctionList& function_list, VADisplay display = nullptr);

    VaapiLoader(goldfish::os::DynamicLibrary lib_va, goldfish::os::DynamicLibrary lib_va_drm,
                int drm_fd, VADisplay display, const VaapiFunctionList& fn_list);

    explicit VaapiLoader(const VaapiFunctionList& fn_list, VADisplay display = nullptr);
    ~VaapiLoader();

    // Move-only
    VaapiLoader(VaapiLoader&&) noexcept;
    VaapiLoader& operator=(VaapiLoader&&) noexcept;
    VaapiLoader(const VaapiLoader&) = delete;
    VaapiLoader& operator=(const VaapiLoader&) = delete;

    const VaapiFunctionList& api() const { return fn_list_; }
    VADisplay display() const { return display_; }

  private:
    goldfish::os::DynamicLibrary lib_va_;
    goldfish::os::DynamicLibrary lib_va_drm_;
    int drm_fd_{-1};
    VADisplay display_{nullptr};
    VaapiFunctionList fn_list_{};
};

}  // namespace goldfish::videobridge
