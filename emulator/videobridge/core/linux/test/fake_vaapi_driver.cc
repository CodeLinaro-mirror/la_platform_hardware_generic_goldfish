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
#include "core/linux/test/fake_vaapi_driver.h"

#include <cstddef>
#include <cstring>

namespace goldfish::videobridge {

FakeVaapiDriver* FakeVaapiDriver::instance_ = nullptr;

FakeVaapiDriver::FakeVaapiDriver() {
    instance_ = this;
    Reset();

    // Synthetic Annex B H.264 NALUs
    idr_bitstream_payload_ = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x1f, 0xda, 0x01, 0x40, 0x16,  // SPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x38, 0x80,                          // PPS
        0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00, 0x10, 0x20, 0x30         // IDR Slice
    };

    delta_bitstream_payload_ = {
        0x00, 0x00, 0x00, 0x01, 0x41, 0x9a, 0x00, 0x10, 0x20, 0x30  // P Slice
    };

    coded_buffer_data_.resize(65536, 0);
    surface_pixel_data_.resize(static_cast<size_t>(1920) * 1080 * 2, 0);
}

FakeVaapiDriver::~FakeVaapiDriver() {
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

void FakeVaapiDriver::Reset() {
    session_active_ = false;
    encode_calls_ = 0;
    force_idr_calls_ = 0;
    reconfigure_calls_ = 0;
    last_bitrate_ = 0;
    last_framerate_ = 0;
    last_profile_ = VAProfileNone;
    last_frame_was_idr_ = false;
}

VaapiFunctionList FakeVaapiDriver::CreateFunctionList() {
    VaapiFunctionList fn_list{};
    fn_list.vaInitialize = &FakeVaapiDriver::Initialize;
    fn_list.vaTerminate = &FakeVaapiDriver::Terminate;
    fn_list.vaErrorStr = &FakeVaapiDriver::ErrorStr;
    fn_list.vaMaxNumProfiles = &FakeVaapiDriver::MaxNumProfiles;
    fn_list.vaQueryConfigProfiles = &FakeVaapiDriver::QueryConfigProfiles;
    fn_list.vaMaxNumEntrypoints = &FakeVaapiDriver::MaxNumEntrypoints;
    fn_list.vaQueryConfigEntrypoints = &FakeVaapiDriver::QueryConfigEntrypoints;
    fn_list.vaGetConfigAttributes = &FakeVaapiDriver::GetConfigAttributes;
    fn_list.vaCreateConfig = &FakeVaapiDriver::CreateConfig;
    fn_list.vaDestroyConfig = &FakeVaapiDriver::DestroyConfig;
    fn_list.vaCreateContext = &FakeVaapiDriver::CreateContext;
    fn_list.vaDestroyContext = &FakeVaapiDriver::DestroyContext;
    fn_list.vaCreateSurfaces = &FakeVaapiDriver::CreateSurfaces;
    fn_list.vaDestroySurfaces = &FakeVaapiDriver::DestroySurfaces;
    fn_list.vaCreateBuffer = &FakeVaapiDriver::CreateBuffer;
    fn_list.vaDestroyBuffer = &FakeVaapiDriver::DestroyBuffer;
    fn_list.vaMapBuffer = &FakeVaapiDriver::MapBuffer;
    fn_list.vaUnmapBuffer = &FakeVaapiDriver::UnmapBuffer;
    fn_list.vaBeginPicture = &FakeVaapiDriver::BeginPicture;
    fn_list.vaRenderPicture = &FakeVaapiDriver::RenderPicture;
    fn_list.vaEndPicture = &FakeVaapiDriver::EndPicture;
    fn_list.vaSyncSurface = &FakeVaapiDriver::SyncSurface;
    fn_list.vaDeriveImage = &FakeVaapiDriver::DeriveImage;
    fn_list.vaDestroyImage = &FakeVaapiDriver::DestroyImage;
    fn_list.vaGetDisplayDRM = &FakeVaapiDriver::GetDisplayDRM;
    return fn_list;
}

VAStatus FakeVaapiDriver::Initialize(VADisplay dpy, int* major_version, int* minor_version) {
    auto* self = static_cast<FakeVaapiDriver*>(dpy);
    if (!self) self = instance_;
    if (self) self->session_active_ = true;
    if (major_version) *major_version = 1;
    if (minor_version) *minor_version = 14;
    return VA_STATUS_SUCCESS;
}

VAStatus FakeVaapiDriver::Terminate(VADisplay dpy) {
    auto* self = static_cast<FakeVaapiDriver*>(dpy);
    if (!self) self = instance_;
    if (self) self->session_active_ = false;
    return VA_STATUS_SUCCESS;
}

const char* FakeVaapiDriver::ErrorStr(VAStatus /*error_status*/) {
    return "FakeVaapiDriver Error";
}

int FakeVaapiDriver::MaxNumProfiles(VADisplay /*dpy*/) {
    return 3;
}

VAStatus FakeVaapiDriver::QueryConfigProfiles(VADisplay /*dpy*/, VAProfile* profile_list,
                                              int* num_profiles) {
    if (!profile_list || !num_profiles) return VA_STATUS_ERROR_INVALID_PARAMETER;
    profile_list[0] = VAProfileH264ConstrainedBaseline;
    profile_list[1] = VAProfileH264Main;
    profile_list[2] = VAProfileH264High;
    *num_profiles = 3;
    return VA_STATUS_SUCCESS;
}

int FakeVaapiDriver::MaxNumEntrypoints(VADisplay /*dpy*/) {
    return 1;
}

VAStatus FakeVaapiDriver::QueryConfigEntrypoints(VADisplay /*dpy*/, VAProfile /*profile*/,
                                                 VAEntrypoint* entrypoint_list,
                                                 int* num_entrypoints) {
    if (!entrypoint_list || !num_entrypoints) return VA_STATUS_ERROR_INVALID_PARAMETER;
    entrypoint_list[0] = VAEntrypointEncSlice;
    *num_entrypoints = 1;
    return VA_STATUS_SUCCESS;
}

VAStatus FakeVaapiDriver::GetConfigAttributes(VADisplay /*dpy*/, VAProfile /*profile*/,
                                              VAEntrypoint /*entrypoint*/,
                                              VAConfigAttrib* attrib_list, int num_attribs) {
    if (!attrib_list || num_attribs <= 0) return VA_STATUS_ERROR_INVALID_PARAMETER;
    for (int i = 0; i < num_attribs; ++i) {
        if (attrib_list[i].type == VAConfigAttribRTFormat) {
            attrib_list[i].value = VA_RT_FORMAT_YUV420;
        } else if (attrib_list[i].type == VAConfigAttribRateControl) {
            attrib_list[i].value = VA_RC_CBR | VA_RC_VBR;
        } else {
            attrib_list[i].value = 0;
        }
    }
    return VA_STATUS_SUCCESS;
}

VAStatus FakeVaapiDriver::CreateConfig(VADisplay /*dpy*/, VAProfile profile,
                                       VAEntrypoint /*entrypoint*/, VAConfigAttrib* /*attrib_list*/,
                                       int /*num_attribs*/, VAConfigID* config_id) {
    if (!config_id) return VA_STATUS_ERROR_INVALID_PARAMETER;
    if (instance_) {
        instance_->last_profile_ = profile;
    }
    *config_id = 0x1001;
    return VA_STATUS_SUCCESS;
}

VAStatus FakeVaapiDriver::DestroyConfig(VADisplay /*dpy*/, VAConfigID /*config_id*/) {
    return VA_STATUS_SUCCESS;
}

VAStatus FakeVaapiDriver::CreateContext(VADisplay /*dpy*/, VAConfigID /*config_id*/,
                                        int /*picture_width*/, int /*picture_height*/, int /*flag*/,
                                        VASurfaceID* /*render_targets*/, int /*num_render_targets*/,
                                        VAContextID* context) {
    if (!context) return VA_STATUS_ERROR_INVALID_PARAMETER;
    *context = 0x2001;
    return VA_STATUS_SUCCESS;
}

VAStatus FakeVaapiDriver::DestroyContext(VADisplay /*dpy*/, VAContextID /*context*/) {
    return VA_STATUS_SUCCESS;
}

VAStatus FakeVaapiDriver::CreateSurfaces(VADisplay /*dpy*/, unsigned int /*format*/,
                                         unsigned int /*width*/, unsigned int /*height*/,
                                         VASurfaceID* surfaces, unsigned int num_surfaces,
                                         VASurfaceAttrib* /*attrib_list*/,
                                         unsigned int /*num_attribs*/) {
    if (!surfaces) return VA_STATUS_ERROR_INVALID_PARAMETER;
    for (unsigned int i = 0; i < num_surfaces; ++i) {
        surfaces[i] = 0x3001 + i;
    }
    return VA_STATUS_SUCCESS;
}

VAStatus FakeVaapiDriver::DestroySurfaces(VADisplay /*dpy*/, VASurfaceID* /*surfaces*/,
                                          int /*num_surfaces*/) {
    return VA_STATUS_SUCCESS;
}

VAStatus FakeVaapiDriver::CreateBuffer(VADisplay dpy, VAContextID /*context*/, VABufferType type,
                                       unsigned int size, unsigned int /*num_elements*/, void* data,
                                       VABufferID* buf_id) {
    auto* self = static_cast<FakeVaapiDriver*>(dpy);
    if (!self) self = instance_;
    if (!buf_id) return VA_STATUS_ERROR_INVALID_PARAMETER;

    *buf_id = 0x4000 + static_cast<uint32_t>(type);

    if (self && type == VAEncMiscParameterBufferType && data &&
        size >= sizeof(VAEncMiscParameterBuffer)) {
        auto* misc = static_cast<VAEncMiscParameterBuffer*>(data);
        if (misc->type == VAEncMiscParameterTypeRateControl) {
            auto* rc = reinterpret_cast<VAEncMiscParameterRateControl*>(misc->data);
            self->last_bitrate_ = rc->bits_per_second;
            self->reconfigure_calls_++;
        } else if (misc->type == VAEncMiscParameterTypeFrameRate) {
            auto* fr = reinterpret_cast<VAEncMiscParameterFrameRate*>(misc->data);
            self->last_framerate_ = fr->framerate;
        }
    }

    if (self && type == VAEncSliceParameterBufferType && data) {
        auto* slice = static_cast<VAEncSliceParameterBufferH264*>(data);
        self->last_frame_was_idr_ = (slice->slice_type == VA_SLICE_I);
    }

    return VA_STATUS_SUCCESS;
}

VAStatus FakeVaapiDriver::DestroyBuffer(VADisplay /*dpy*/, VABufferID /*buffer_id*/) {
    return VA_STATUS_SUCCESS;
}

VAStatus FakeVaapiDriver::MapBuffer(VADisplay dpy, VABufferID buf_id, void** pbuf) {
    auto* self = static_cast<FakeVaapiDriver*>(dpy);
    if (!self) self = instance_;
    if (!pbuf || !self) return VA_STATUS_ERROR_INVALID_PARAMETER;

    if (buf_id == 0x4000 + static_cast<uint32_t>(VAEncCodedBufferType)) {
        const auto& src = self->last_frame_was_idr_ ? self->idr_bitstream_payload_
                                                    : self->delta_bitstream_payload_;
        std::memcpy(self->coded_buffer_data_.data(), src.data(), src.size());
        self->mock_coded_segment_.size = static_cast<uint32_t>(src.size());
        self->mock_coded_segment_.bit_offset = 0;
        self->mock_coded_segment_.status = VA_STATUS_SUCCESS;
        self->mock_coded_segment_.reserved = 0;
        self->mock_coded_segment_.buf = self->coded_buffer_data_.data();
        self->mock_coded_segment_.next = nullptr;
        *pbuf = &self->mock_coded_segment_;
    } else {
        *pbuf = self->surface_pixel_data_.data();
    }
    return VA_STATUS_SUCCESS;
}

VAStatus FakeVaapiDriver::UnmapBuffer(VADisplay /*dpy*/, VABufferID /*buf_id*/) {
    return VA_STATUS_SUCCESS;
}

VAStatus FakeVaapiDriver::BeginPicture(VADisplay /*dpy*/, VAContextID /*context*/,
                                       VASurfaceID /*render_target*/) {
    return VA_STATUS_SUCCESS;
}

VAStatus FakeVaapiDriver::RenderPicture(VADisplay dpy, VAContextID /*context*/, VABufferID* buffers,
                                        int num_buffers) {
    auto* self = static_cast<FakeVaapiDriver*>(dpy);
    if (!self) self = instance_;
    if (!buffers || num_buffers <= 0) return VA_STATUS_ERROR_INVALID_PARAMETER;
    return VA_STATUS_SUCCESS;
}

VAStatus FakeVaapiDriver::EndPicture(VADisplay dpy, VAContextID /*context*/) {
    auto* self = static_cast<FakeVaapiDriver*>(dpy);
    if (!self) self = instance_;
    if (self) {
        self->encode_calls_++;
        if (self->last_frame_was_idr_) {
            self->force_idr_calls_++;
        }
    }
    return VA_STATUS_SUCCESS;
}

VAStatus FakeVaapiDriver::SyncSurface(VADisplay /*dpy*/, VASurfaceID /*render_target*/) {
    return VA_STATUS_SUCCESS;
}

VAStatus FakeVaapiDriver::DeriveImage(VADisplay dpy, VASurfaceID /*surface*/, VAImage* image) {
    auto* self = static_cast<FakeVaapiDriver*>(dpy);
    if (!self) self = instance_;
    if (!image) return VA_STATUS_ERROR_INVALID_PARAMETER;

    image->image_id = 0x5001;
    image->buf = 0x4050;
    image->width = 1280;
    image->height = 720;
    image->pitches[0] = 1280;
    image->pitches[1] = 1280;
    image->offsets[0] = 0;
    image->offsets[1] = 1280 * 720;
    image->format.fourcc = VA_FOURCC_NV12;
    return VA_STATUS_SUCCESS;
}

VAStatus FakeVaapiDriver::DestroyImage(VADisplay /*dpy*/, VAImageID /*image*/) {
    return VA_STATUS_SUCCESS;
}

VADisplay FakeVaapiDriver::GetDisplayDRM(int /*fd*/) {
    return reinterpret_cast<VADisplay>(instance_);
}

}  // namespace goldfish::videobridge
