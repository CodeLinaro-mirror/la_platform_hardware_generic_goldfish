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
using VAGenericFunc = void (*)();

inline constexpr size_t VA_PADDING_LOW = 4;
inline constexpr size_t VA_PADDING_MEDIUM = 8;

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
    VAEncCodedBufferType = 21,
    VAEncSequenceParameterBufferType = 22,
    VAEncPictureParameterBufferType = 23,
    VAEncSliceParameterBufferType = 24,
    VAEncPackedHeaderParameterBufferType = 25,
    VAEncPackedHeaderDataBufferType = 26,
    VAEncMiscParameterBufferType = 27,
};

enum VAEncPackedHeaderType : int32_t {
    VAEncPackedHeaderSequence = 1,
    VAEncPackedHeaderPicture = 2,
    VAEncPackedHeaderSlice = 3,
    VAEncPackedHeaderRawData = 4,
};

inline constexpr uint32_t VA_ENC_PACKED_HEADER_NONE = 0x00000000U;
inline constexpr uint32_t VA_ENC_PACKED_HEADER_SEQUENCE = 0x00000001U;
inline constexpr uint32_t VA_ENC_PACKED_HEADER_PICTURE = 0x00000002U;
inline constexpr uint32_t VA_ENC_PACKED_HEADER_SLICE = 0x00000004U;
inline constexpr uint32_t VA_ENC_PACKED_HEADER_MISC = 0x00000008U;
inline constexpr uint32_t VA_ENC_PACKED_HEADER_RAW_DATA = 0x00000010U;

struct VAEncPackedHeaderParameterBuffer {
    uint32_t type;
    uint32_t bit_length;
    uint8_t has_emulation_bytes;
    uint32_t va_reserved[VA_PADDING_LOW];
};

enum VAEncMiscParameterType : int32_t {
    VAEncMiscParameterTypeFrameRate = 0,
    VAEncMiscParameterTypeRateControl = 1,
    VAEncMiscParameterTypeMaxSliceSize = 2,
    VAEncMiscParameterTypeAIR = 3,
    VAEncMiscParameterTypeMaxFrameSize = 4,
    VAEncMiscParameterTypeHRD = 5,
};

inline constexpr uint32_t VA_FOURCC_NV12 = 0x3231564EU;  // 'NV12'
inline constexpr uint32_t VA_RT_FORMAT_YUV420 = 0x00000001U;
inline constexpr uint32_t VA_RC_NONE = 0x00000001U;
inline constexpr uint32_t VA_RC_CBR = 0x00000002U;
inline constexpr uint32_t VA_RC_VBR = 0x00000004U;
inline constexpr uint32_t VA_RC_CQP = 0x00000010U;

inline constexpr uint32_t VA_SURFACE_ATTRIB_PIXEL_FORMAT = 0x00000002U;
inline constexpr uint32_t VA_SURFACE_ATTRIB_SETTABLE = 0x00000002U;

enum VASurfaceAttribType : int32_t {
    VASurfaceAttribNone = 0,
    VASurfaceAttribPixelFormat = 1,
    VASurfaceAttribMemoryType = 2,
};

enum VAGenericValueType : int32_t {
    VAGenericValueTypeInteger = 1,
    VAGenericValueTypeFloat = 2,
    VAGenericValueTypePointer = 3,
    VAGenericValueTypeFunc = 4,
};

struct VAGenericValue {
    VAGenericValueType type;
    union {
        int32_t i;
        float f;
        void* p;
        VAGenericFunc fn;
        uint32_t value;
    } value;
};

struct VASurfaceAttrib {
    VASurfaceAttribType type;
    uint32_t flags;
    VAGenericValue value;
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
    uint32_t va_reserved[VA_PADDING_LOW];
};

struct VAImage {
    VAImageID image_id;
    VAImageFormat format;
    VABufferID buf;
    uint16_t width;
    uint16_t height;
    uint32_t data_size;
    uint32_t num_planes;
    uint32_t pitches[3];
    uint32_t offsets[3];
    int32_t num_palette_entries;
    int32_t entry_bytes;
    int8_t component_order[4];
    uint32_t va_reserved[VA_PADDING_LOW];
};

struct VAPictureH264 {
    VASurfaceID picture_id;
    uint32_t frame_num;
    uint32_t flags;
    int32_t TopFieldOrderCnt;
    int32_t BottomFieldOrderCnt;
    uint32_t va_reserved[VA_PADDING_LOW];
};

inline constexpr uint32_t VA_PICTURE_H264_INVALID = 0x00000001U;
inline constexpr uint32_t VA_PICTURE_H264_TOP_FIELD = 0x00000002U;
inline constexpr uint32_t VA_PICTURE_H264_BOTTOM_FIELD = 0x00000004U;
inline constexpr uint32_t VA_PICTURE_H264_SHORT_TERM_REFERENCE = 0x00000008U;
inline constexpr uint32_t VA_PICTURE_H264_LONG_TERM_REFERENCE = 0x00000010U;

struct VAEncSequenceParameterBufferH264 {
    uint8_t seq_parameter_set_id;
    uint8_t level_idc;
    uint32_t intra_period;
    uint32_t intra_idr_period;
    uint32_t ip_period;
    uint32_t bits_per_second;
    uint32_t max_num_ref_frames;
    uint16_t picture_width_in_mbs;
    uint16_t picture_height_in_mbs;

    union {
        struct {
            uint32_t chroma_format_idc : 2;
            uint32_t frame_mbs_only_flag : 1;
            uint32_t mb_adaptive_frame_field_flag : 1;
            uint32_t seq_scaling_matrix_present_flag : 1;
            uint32_t direct_8x8_inference_flag : 1;
            uint32_t log2_max_frame_num_minus4 : 4;
            uint32_t pic_order_cnt_type : 2;
            uint32_t log2_max_pic_order_cnt_lsb_minus4 : 4;
            uint32_t delta_pic_order_always_zero_flag : 1;
        } bits;
        uint32_t value;
    } seq_fields;

    uint8_t bit_depth_luma_minus8;
    uint8_t bit_depth_chroma_minus8;
    uint8_t num_ref_frames_in_pic_order_cnt_cycle;
    int32_t offset_for_non_ref_pic;
    int32_t offset_for_top_to_bottom_field;
    int32_t offset_for_ref_frame[256];

    uint8_t frame_cropping_flag;
    uint32_t frame_crop_left_offset;
    uint32_t frame_crop_right_offset;
    uint32_t frame_crop_top_offset;
    uint32_t frame_crop_bottom_offset;

    uint8_t vui_parameters_present_flag;
    union {
        struct {
            uint32_t aspect_ratio_info_present_flag : 1;
            uint32_t timing_info_present_flag : 1;
            uint32_t bitstream_restriction_flag : 1;
            uint32_t log2_max_mv_length_horizontal : 5;
            uint32_t log2_max_mv_length_vertical : 5;
            uint32_t fixed_frame_rate_flag : 1;
            uint32_t low_delay_hrd_flag : 1;
            uint32_t motion_vectors_over_pic_boundaries_flag : 1;
            uint32_t reserved : 16;
        } bits;
        uint32_t value;
    } vui_fields;
    uint8_t aspect_ratio_idc;
    uint32_t sar_width;
    uint32_t sar_height;
    uint32_t num_units_in_tick;
    uint32_t time_scale;

    uint32_t va_reserved[VA_PADDING_LOW];
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
    int8_t chroma_qp_index_offset;
    int8_t second_chroma_qp_index_offset;

    union {
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
            uint32_t pic_order_present_flag : 1;
            uint32_t pic_scaling_matrix_present_flag : 1;
        } bits;
        uint32_t value;
    } pic_fields;

    uint32_t va_reserved[VA_PADDING_LOW];
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
    uint16_t idr_pic_id;
    uint16_t pic_order_cnt_lsb;
    int32_t delta_pic_order_cnt_bottom;
    int32_t delta_pic_order_cnt[2];
    uint8_t direct_spatial_mv_pred_flag;
    uint8_t num_ref_idx_active_override_flag;
    uint8_t num_ref_idx_l0_active_minus1;
    uint8_t num_ref_idx_l1_active_minus1;
    VAPictureH264 RefPicList0[32];
    VAPictureH264 RefPicList1[32];
    uint8_t luma_log2_weight_denom;
    uint8_t chroma_log2_weight_denom;
    uint8_t luma_weight_l0_flag;
    int16_t luma_weight_l0[32];
    int16_t luma_offset_l0[32];
    uint8_t chroma_weight_l0_flag;
    int16_t chroma_weight_l0[32][2];
    int16_t chroma_offset_l0[32][2];
    uint8_t luma_weight_l1_flag;
    int16_t luma_weight_l1[32];
    int16_t luma_offset_l1[32];
    uint8_t chroma_weight_l1_flag;
    int16_t chroma_weight_l1[32][2];
    int16_t chroma_offset_l1[32][2];
    uint8_t cabac_init_idc;
    int8_t slice_qp_delta;
    uint8_t disable_deblocking_filter_idc;
    int8_t slice_alpha_c0_offset_div2;
    int8_t slice_beta_offset_div2;
    uint32_t va_reserved[VA_PADDING_LOW];
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
    union {
        struct {
            uint32_t reset : 1;
            uint32_t disable_frame_skip : 1;
            uint32_t disable_bit_stuffing : 1;
            uint32_t mb_rate_control : 4;
            uint32_t temporal_id : 8;
            uint32_t cfs_I_frames : 1;
            uint32_t enable_parallel_brc : 1;
            uint32_t enable_dynamic_scaling : 1;
            uint32_t frame_tolerance_mode : 2;
            uint32_t reserved : 12;
        } bits;
        uint32_t value;
    } rc_flags;
    uint32_t ICQ_quality_factor;
    uint32_t max_qp;
    uint32_t quality_factor;
    uint32_t target_frame_size;
    uint32_t va_reserved[VA_PADDING_LOW];
};

struct VAEncMiscParameterFrameRate {
    uint32_t framerate;
    union {
        struct {
            uint32_t temporal_id : 8;
            uint32_t reserved : 24;
        } bits;
        uint32_t value;
    } framerate_flags;
    uint32_t va_reserved[VA_PADDING_LOW];
};

struct VAEncMiscParameterHRD {
    uint32_t initial_buffer_fullness;
    uint32_t buffer_size;
    uint32_t va_reserved[VA_PADDING_LOW];
};

/**
 * @struct VACodedBufferSegment
 * @brief Coded buffer segment returned when mapping a VAEncCodedBufferType buffer on hardware
 * drivers.
 */
struct VACodedBufferSegment {
    uint32_t size;
    uint32_t bit_offset;
    uint32_t status;
    uint32_t reserved;
    void* buf;
    void* next;
    uint32_t va_reserved[VA_PADDING_LOW];
};

static_assert(sizeof(VAImageFormat) == 48, "VAImageFormat size must match libva ABI");
static_assert(sizeof(VAImage) == 120, "VAImage size must match libva ABI");
static_assert(offsetof(VAImage, pitches) == 68, "VAImage::pitches offset must match libva ABI");
static_assert(offsetof(VAImage, offsets) == 80, "VAImage::offsets offset must match libva ABI");
static_assert(sizeof(VAPictureH264) == 36, "VAPictureH264 size must match libva ABI");
static_assert(sizeof(VAEncSequenceParameterBufferH264) == 1132,
              "VAEncSequenceParameterBufferH264 size must match libva ABI");
static_assert(offsetof(VAEncPictureParameterBufferH264, coded_buf) == 612,
              "VAEncPictureParameterBufferH264::coded_buf offset must match libva ABI");
static_assert(sizeof(VAEncPictureParameterBufferH264) == 648,
              "VAEncPictureParameterBufferH264 size must match libva ABI");
static_assert(sizeof(VAEncSliceParameterBufferH264) == 3140,
              "VAEncSliceParameterBufferH264 size must match libva ABI");
static_assert(sizeof(VAEncMiscParameterRateControl) == 60,
              "VAEncMiscParameterRateControl size must match libva ABI");

}  // namespace goldfish::videobridge
