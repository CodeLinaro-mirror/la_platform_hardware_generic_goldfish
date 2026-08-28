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

#include <cstddef>
#include <cstdint>

namespace goldfish::videobridge {

using VADisplay = void*;
using VASurfaceID = uint32_t;
using VAContextID = uint32_t;
using VAConfigID = uint32_t;
using VABufferID = uint32_t;
using VAImageID = uint32_t;
using VAStatus = int32_t;

inline constexpr uint32_t VA_INVALID_ID = 0xFFFFFFFFU;
inline constexpr uint32_t VA_INVALID_SURFACE = 0xFFFFFFFFU;

inline constexpr VAStatus VA_STATUS_SUCCESS = 0x00000000;
inline constexpr VAStatus VA_STATUS_ERROR_OPERATION_FAILED = 0x00000001;
inline constexpr VAStatus VA_STATUS_ERROR_ALLOCATION_FAILED = 0x00000002;
inline constexpr VAStatus VA_STATUS_ERROR_INVALID_DISPLAY = 0x00000003;
inline constexpr VAStatus VA_STATUS_ERROR_INVALID_CONFIG = 0x00000004;
inline constexpr VAStatus VA_STATUS_ERROR_INVALID_CONTEXT = 0x00000005;
inline constexpr VAStatus VA_STATUS_ERROR_INVALID_SURFACE = 0x00000006;
inline constexpr VAStatus VA_STATUS_ERROR_INVALID_BUFFER = 0x00000007;
inline constexpr VAStatus VA_STATUS_ERROR_INVALID_IMAGE = 0x00000008;
inline constexpr VAStatus VA_STATUS_ERROR_INVALID_PARAMETER = 0x0000000C;
inline constexpr VAStatus VA_STATUS_ERROR_UNSUPPORTED_PROFILE = 0x0000000D;
inline constexpr VAStatus VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT = 0x0000000E;
inline constexpr VAStatus VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE = 0x00000010;
inline constexpr VAStatus VA_STATUS_ERROR_SURFACE_BUSY = 0x00000012;
inline constexpr VAStatus VA_STATUS_ERROR_UNIMPLEMENTED = 0x00000014;
inline constexpr VAStatus VA_STATUS_ERROR_NOT_ENOUGH_BUFFER = 0x00000020;
inline constexpr VAStatus VA_STATUS_ERROR_UNKNOWN = 0xFFFFFFFF;

enum VAProfile : int32_t {
    VAProfileNone = -1,
    VAProfileH264Baseline = 5,
    VAProfileH264Main = 6,
    VAProfileH264High = 7,
    VAProfileH264ConstrainedBaseline = 13,
};

enum VAEntrypoint : int32_t {
    VAEntrypointVLD = 1,
    VAEntrypointEncSlice = 6,
    VAEntrypointEncPicture = 7,
    VAEntrypointEncSliceLP = 8,
};

enum VAConfigAttribType : int32_t {
    VAConfigAttribRTFormat = 0,
    VAConfigAttribRateControl = 5,
    VAConfigAttribEncPackedHeaders = 10,
    VAConfigAttribEncMaxRefFrames = 13,
};

struct VAConfigAttrib {
    VAConfigAttribType type;
    uint32_t value;
};

enum VABufferType : int32_t {
    VAEncSequenceParameterBufferType = 21,
    VAEncPictureParameterBufferType = 22,
    VAEncSliceParameterBufferType = 23,
    VAEncPackedHeaderParameterBufferType = 24,
    VAEncPackedHeaderDataBufferType = 25,
    VAEncMiscParameterBufferType = 26,
    VAEncCodedBufferType = 27,
};

enum VAEncMiscParameterType : int32_t {
    VAEncMiscParameterTypeFrameRate = 0,
    VAEncMiscParameterTypeRateControl = 1,
    VAEncMiscParameterTypeMaxSliceSize = 2,
    VAEncMiscParameterTypeHRD = 4,
};

inline constexpr uint32_t VA_FOURCC_NV12 = 0x3231564EU;  // 'NV12'
inline constexpr uint32_t VA_RT_FORMAT_YUV420 = 0x00000001U;
inline constexpr uint32_t VA_RC_CBR = 0x00000002U;
inline constexpr uint32_t VA_RC_VBR = 0x00000004U;

inline constexpr uint32_t VA_SURFACE_ATTRIB_PIXEL_FORMAT = 0x00000002U;
inline constexpr uint32_t VA_SURFACE_ATTRIB_SETTABLE = 0x00000002U;

enum VASurfaceAttribType : int32_t {
    VASurfaceAttribNone = 0,
    VASurfaceAttribPixelFormat = 1,
    VASurfaceAttribMemoryType = 2,
};

struct VASurfaceAttrib {
    VASurfaceAttribType type;
    uint32_t flags;
    struct {
        uint32_t type;
        union {
            int32_t i;
            float f;
            void* p;
            uint32_t value;
        } value;
    } value;
};

struct VAImageFormat {
    uint32_t fourcc;
    uint32_t byte_order;
    uint32_t bits_per_pixel;
    uint32_t depth;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t alpha_mask;
};

struct VAImage {
    VAImageID image_id;
    VAImageFormat format;
    VABufferID buf;
    uint32_t width;
    uint32_t height;
    uint32_t data_size;
    uint32_t num_planes;
    uint32_t pitches[3];
    uint32_t offsets[3];
    uint32_t entry_bytes[3];
    uint32_t component_order[4];
};

struct VAPictureH264 {
    VASurfaceID picture_id;
    uint32_t frame_num;
    uint32_t flags;
    uint32_t TopFieldOrderCnt;
    uint32_t BottomFieldOrderCnt;
};

inline constexpr uint32_t VA_PICTURE_H264_TOP_FIELD = 0x00000001U;
inline constexpr uint32_t VA_PICTURE_H264_BOTTOM_FIELD = 0x00000002U;
inline constexpr uint32_t VA_PICTURE_H264_INVALID = 0x00000004U;

struct VAEncSequenceParameterBufferH264 {
    uint8_t seq_parameter_set_id;
    uint8_t level_idc;
    uint32_t intra_period;
    uint32_t intra_idr_period;
    uint32_t ip_period;
    uint32_t bits_per_second;
    uint32_t max_num_ref_frames;
    uint32_t picture_width_in_mbs;
    uint32_t picture_height_in_mbs;
    struct {
        uint32_t chroma_format_idc : 2;
        uint32_t frame_mbs_only_flag : 1;
        uint32_t mb_adaptive_frame_field_flag : 1;
        uint32_t direct_8x8_inference_flag : 1;
        uint32_t log2_max_frame_num_minus4 : 4;
        uint32_t pic_order_cnt_type : 3;
        uint32_t log2_max_pic_order_cnt_lsb_minus4 : 4;
        uint32_t delta_pic_order_always_zero_flag : 1;
        uint32_t reserved : 16;
    } seq_fields;
    uint32_t time_scale;
    uint32_t num_units_in_tick;
};

struct VAEncPictureParameterBufferH264 {
    VAPictureH264 CurrPic;
    VAPictureH264 ReferenceFrames[16];
    VABufferID coded_buf;
    uint8_t pic_parameter_set_id;
    uint8_t seq_parameter_set_id;
    uint8_t last_picture;
    uint16_t frame_num;
    uint8_t pic_init_qp;
    uint8_t num_ref_idx_l0_active_minus1;
    uint8_t num_ref_idx_l1_active_minus1;
    struct {
        uint32_t idr_pic_flag : 1;
        uint32_t reference_pic_flag : 2;
        uint32_t entropy_coding_mode_flag : 1;
        uint32_t weighted_pred_flag : 1;
        uint32_t weighted_bipred_idc : 2;
        uint32_t constrained_intra_pred_flag : 1;
        uint32_t transform_8x8_mode_flag : 1;
        uint32_t deblocking_filter_control_present_flag : 1;
        uint32_t redundant_pic_cnt_present_flag : 1;
        uint32_t bottom_field_pic_order_in_frame_present_flag : 1;
        uint32_t pic_order_present_flag : 1;
        uint32_t pic_scaling_matrix_present_flag : 1;
        uint32_t reserved : 18;
    } pic_fields;
};

enum VAEncSliceTypeH264 : int32_t {
    VA_SLICE_P = 0,
    VA_SLICE_B = 1,
    VA_SLICE_I = 2,
};

struct VAEncSliceParameterBufferH264 {
    uint32_t macroblock_address;
    uint32_t num_macroblocks;
    VABufferID macroblock_info;
    uint8_t slice_type;
    uint8_t pic_parameter_set_id;
    uint8_t idr_pic_id;
    uint16_t pic_order_cnt_lsb;
    int32_t delta_pic_order_cnt_bottom;
    int32_t delta_pic_order_cnt[2];
    uint8_t direct_spatial_mv_pred_flag;
    uint8_t num_ref_idx_active_override_flag;
    uint8_t num_ref_idx_l0_active_minus1;
    uint8_t num_ref_idx_l1_active_minus1;
    VAPictureH264 RefPicList0[32];
    VAPictureH264 RefPicList1[32];
    uint8_t slice_qp_delta;
    uint8_t disable_deblocking_filter_idc;
    int8_t slice_alpha_c0_offset_div2;
    int8_t slice_beta_offset_div2;
    uint8_t reserved[2];
};

struct VAEncMiscParameterBuffer {
    VAEncMiscParameterType type;
    uint32_t data[0];
};

struct VAEncMiscParameterRateControl {
    uint32_t bits_per_second;
    uint32_t target_percentage;
    uint32_t window_size;
    uint32_t initial_qp;
    uint32_t min_qp;
    uint32_t basic_unit_size;
    struct {
        uint32_t reset : 1;
        uint32_t disable_frame_skipping : 1;
        uint32_t disable_bit_stuffing : 1;
        uint32_t mb_rate_control : 4;
        uint32_t reserved : 25;
    } rc_flags;
};

struct VAEncMiscParameterFrameRate {
    uint32_t framerate;
};

/**
 * @struct VACodedBufferSegment
 * @brief Coded buffer segment returned when mapping a VAEncCodedBufferType buffer on hardware
 * drivers.
 */
struct VACodedBufferSegment {
    uint32_t size;
    uint32_t bit_offset;
    VAStatus status;
    uint32_t reserved;
    void* buf;
    void* next;
};

}  // namespace goldfish::videobridge
