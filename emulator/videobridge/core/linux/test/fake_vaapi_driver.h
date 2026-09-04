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

#include <cstdint>
#include <vector>

#include "core/linux/va_api_types.h"
#include "core/linux/vaapi_loader.h"

namespace goldfish::videobridge {

/**
 * @class FakeVaapiDriver
 * @brief In-memory mock implementation of VA-API for hermetic unit testing.
 */
class FakeVaapiDriver {
  public:
    FakeVaapiDriver();
    ~FakeVaapiDriver();

    VaapiFunctionList CreateFunctionList();
    VADisplay display() const {
        return reinterpret_cast<VADisplay>(const_cast<FakeVaapiDriver*>(this));
    }

    // Test inspection queries
    int encode_calls() const { return encode_calls_; }
    int force_idr_calls() const { return force_idr_calls_; }
    int reconfigure_calls() const { return reconfigure_calls_; }
    uint32_t last_bitrate() const { return last_bitrate_; }
    uint32_t last_framerate() const { return last_framerate_; }
    VAProfile last_profile() const { return last_profile_; }
    bool session_active() const { return session_active_; }

    void Reset();

  private:
    static VAStatus Initialize(VADisplay dpy, int* major_version, int* minor_version);
    static VAStatus Terminate(VADisplay dpy);
    static const char* ErrorStr(VAStatus error_status);
    static int MaxNumProfiles(VADisplay dpy);
    static VAStatus QueryConfigProfiles(VADisplay dpy, VAProfile* profile_list, int* num_profiles);
    static int MaxNumEntrypoints(VADisplay dpy);
    static VAStatus QueryConfigEntrypoints(VADisplay dpy, VAProfile profile,
                                           VAEntrypoint* entrypoint_list, int* num_entrypoints);
    static VAStatus GetConfigAttributes(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint,
                                        VAConfigAttrib* attrib_list, int num_attribs);
    static VAStatus CreateConfig(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint,
                                 VAConfigAttrib* attrib_list, int num_attribs,
                                 VAConfigID* config_id);
    static VAStatus DestroyConfig(VADisplay dpy, VAConfigID config_id);
    static VAStatus CreateContext(VADisplay dpy, VAConfigID config_id, int picture_width,
                                  int picture_height, int flag, VASurfaceID* render_targets,
                                  int num_render_targets, VAContextID* context);
    static VAStatus DestroyContext(VADisplay dpy, VAContextID context);
    static VAStatus CreateSurfaces(VADisplay dpy, unsigned int format, unsigned int width,
                                   unsigned int height, VASurfaceID* surfaces,
                                   unsigned int num_surfaces, VASurfaceAttrib* attrib_list,
                                   unsigned int num_attribs);
    static VAStatus DestroySurfaces(VADisplay dpy, VASurfaceID* surfaces, int num_surfaces);
    static VAStatus CreateBuffer(VADisplay dpy, VAContextID context, VABufferType type,
                                 unsigned int size, unsigned int num_elements, void* data,
                                 VABufferID* buf_id);
    static VAStatus DestroyBuffer(VADisplay dpy, VABufferID buffer_id);
    static VAStatus MapBuffer(VADisplay dpy, VABufferID buf_id, void** pbuf);
    static VAStatus UnmapBuffer(VADisplay dpy, VABufferID buf_id);
    static VAStatus BeginPicture(VADisplay dpy, VAContextID context, VASurfaceID render_target);
    static VAStatus RenderPicture(VADisplay dpy, VAContextID context, VABufferID* buffers,
                                  int num_buffers);
    static VAStatus EndPicture(VADisplay dpy, VAContextID context);
    static VAStatus SyncSurface(VADisplay dpy, VASurfaceID render_target);
    static VAStatus DeriveImage(VADisplay dpy, VASurfaceID surface, VAImage* image);
    static VAStatus DestroyImage(VADisplay dpy, VAImageID image);
    static VADisplay GetDisplayDRM(int fd);

    static FakeVaapiDriver* instance_;

    bool session_active_{false};
    int encode_calls_{0};
    int force_idr_calls_{0};
    int reconfigure_calls_{0};
    uint32_t last_bitrate_{0};
    uint32_t last_framerate_{0};
    VAProfile last_profile_{VAProfileNone};
    bool last_frame_was_idr_{false};

    std::vector<uint8_t> idr_bitstream_payload_;
    std::vector<uint8_t> delta_bitstream_payload_;
    std::vector<uint8_t> coded_buffer_data_;
    std::vector<uint8_t> surface_pixel_data_;
    VACodedBufferSegment mock_coded_segment_{};
};

}  // namespace goldfish::videobridge
