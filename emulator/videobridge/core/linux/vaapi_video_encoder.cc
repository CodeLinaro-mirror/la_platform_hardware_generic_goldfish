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
#include "core/linux/vaapi_video_encoder.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"

#include "api/video/i420_buffer.h"
#include "api/video/nv12_buffer.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/h264_profile_level_id.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "core/linux/frame_nv12_converter.h"
#include "core/linux/h264_format_utils.h"
#include "libyuv/convert.h"
#include "libyuv/planar_functions.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/logging.h"

namespace goldfish::videobridge {

namespace {
constexpr uint32_t kDefaultTargetBitrateBps = 2000000;  // 2 Mbps
constexpr uint32_t kDefaultMaxFramerateFps = 30;
constexpr size_t kMaxCodedBufferSize = 4ULL * 1024ULL * 1024ULL;  // 4 MB
constexpr uint32_t kMaxSupportedDimension = 4096;

/**
 * @class BitWriter
 * @brief Minimal Exp-Golomb bit writer for generating H.264 Annex-B SPS and PPS RBSP payloads.
 */
class BitWriter {
  public:
    void PutBit(uint32_t bit) {
        cur_byte_ = static_cast<uint8_t>((cur_byte_ << 1) | (bit & 1U));
        if (++bit_pos_ == 8) {
            FlushByte(cur_byte_);
            cur_byte_ = 0;
            bit_pos_ = 0;
        }
    }

    void PutBits(uint32_t val, int num_bits) {
        for (int i = num_bits - 1; i >= 0; --i) {
            PutBit((val >> i) & 1U);
        }
    }

    void PutUE(uint32_t val) {
        uint32_t code_num = val + 1;
        int leading_zero_bits = -1;
        for (uint32_t tmp = code_num; tmp > 0; tmp >>= 1) {
            ++leading_zero_bits;
        }
        for (int i = 0; i < leading_zero_bits; ++i) {
            PutBit(0);
        }
        PutBits(code_num, leading_zero_bits + 1);
    }

    void PutSE(int32_t val) {
        uint32_t code_num =
                (val <= 0) ? static_cast<uint32_t>(-2 * val) : static_cast<uint32_t>(2 * val - 1);
        PutUE(code_num);
    }

    void WriteRbspTrailingBits() {
        PutBit(1);
        while (bit_pos_ != 0) {
            PutBit(0);
        }
    }

    std::vector<uint8_t> BuildAnnexBNalu(uint8_t nal_header) const {
        std::vector<uint8_t> out = {0x00, 0x00, 0x00, 0x01, nal_header};
        out.insert(out.end(), bytes_.begin(), bytes_.end());
        return out;
    }

  private:
    void FlushByte(uint8_t b) {
        if (zero_run_ >= 2 && b <= 0x03) {
            bytes_.push_back(0x03);
            zero_run_ = 0;
        }
        bytes_.push_back(b);
        if (b == 0x00) {
            ++zero_run_;
        } else {
            zero_run_ = 0;
        }
    }

    std::vector<uint8_t> bytes_;
    uint8_t cur_byte_{0};
    int bit_pos_{0};
    int zero_run_{0};
};

uint8_t VaapiProfileToIdc(VAProfile profile) {
    switch (profile) {
    case VAProfileH264Main:
        return 77;
    case VAProfileH264High:
        return 100;
    case VAProfileH264ConstrainedBaseline:
    case VAProfileH264Baseline:
    default:
        return 66;
    }
}

uint8_t VaapiProfileToConstraintFlags(VAProfile profile) {
    switch (profile) {
    case VAProfileH264Main:
        return 0x40;  // constraint_set1_flag
    case VAProfileH264High:
        return 0x00;
    case VAProfileH264ConstrainedBaseline:
    case VAProfileH264Baseline:
    default:
        return 0xC0;  // constraint_set0_flag | constraint_set1_flag
    }
}

std::vector<uint8_t> BuildAnnexBSpsNalu(VAProfile profile,
                                        const VAEncSequenceParameterBufferH264& seq) {
    BitWriter bw;
    const uint8_t profile_idc = VaapiProfileToIdc(profile);
    const uint8_t constraint_flags = VaapiProfileToConstraintFlags(profile);
    bw.PutBits(profile_idc, 8);
    bw.PutBits(constraint_flags, 8);
    bw.PutBits(seq.level_idc, 8);
    bw.PutUE(seq.seq_parameter_set_id);

    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244 ||
        profile_idc == 44 || profile_idc == 83 || profile_idc == 86 || profile_idc == 118 ||
        profile_idc == 128) {
        bw.PutUE(seq.seq_fields.bits.chroma_format_idc);
        if (seq.seq_fields.bits.chroma_format_idc == 3) {
            bw.PutBit(0);
        }
        bw.PutUE(seq.bit_depth_luma_minus8);
        bw.PutUE(seq.bit_depth_chroma_minus8);
        bw.PutBit(0);  // qpprime_y_zero_transform_bypass_flag
        bw.PutBit(0);  // seq_scaling_matrix_present_flag
    }

    bw.PutUE(seq.seq_fields.bits.log2_max_frame_num_minus4);
    bw.PutUE(seq.seq_fields.bits.pic_order_cnt_type);
    if (seq.seq_fields.bits.pic_order_cnt_type == 0) {
        bw.PutUE(seq.seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4);
    }
    bw.PutUE(seq.max_num_ref_frames);
    bw.PutBit(0);  // gaps_in_frame_num_value_allowed_flag
    bw.PutUE(seq.picture_width_in_mbs - 1);
    bw.PutUE(seq.picture_height_in_mbs - 1);
    bw.PutBit(seq.seq_fields.bits.frame_mbs_only_flag);
    if (!seq.seq_fields.bits.frame_mbs_only_flag) {
        bw.PutBit(seq.seq_fields.bits.mb_adaptive_frame_field_flag);
    }
    bw.PutBit(seq.seq_fields.bits.direct_8x8_inference_flag);
    bw.PutBit(seq.frame_cropping_flag);
    if (seq.frame_cropping_flag) {
        bw.PutUE(seq.frame_crop_left_offset);
        bw.PutUE(seq.frame_crop_right_offset);
        bw.PutUE(seq.frame_crop_top_offset);
        bw.PutUE(seq.frame_crop_bottom_offset);
    }
    bw.PutBit(seq.vui_parameters_present_flag);
    if (seq.vui_parameters_present_flag) {
        bw.PutBit(0);  // aspect_ratio_info_present_flag
        bw.PutBit(0);  // overscan_info_present_flag
        bw.PutBit(0);  // video_signal_type_present_flag
        bw.PutBit(0);  // chroma_loc_info_present_flag
        bw.PutBit(seq.vui_fields.bits.timing_info_present_flag);
        if (seq.vui_fields.bits.timing_info_present_flag) {
            bw.PutBits(seq.num_units_in_tick, 32);
            bw.PutBits(seq.time_scale, 32);
            bw.PutBit(1);  // fixed_frame_rate_flag
        }
        bw.PutBit(0);  // nal_hrd_parameters_present_flag
        bw.PutBit(0);  // vcl_hrd_parameters_present_flag
        bw.PutBit(0);  // pic_struct_present_flag
        bw.PutBit(seq.vui_fields.bits.bitstream_restriction_flag);
        if (seq.vui_fields.bits.bitstream_restriction_flag) {
            bw.PutBit(1);  // motion_vectors_over_pic_boundaries_flag
            bw.PutUE(0);   // max_bytes_per_pic_denom
            bw.PutUE(0);   // max_bits_per_mb_denom
            bw.PutUE(11);  // log2_max_mv_length_horizontal
            bw.PutUE(11);  // log2_max_mv_length_vertical
            bw.PutUE(0);   // max_num_reorder_frames (0 for ultra-low latency!)
            bw.PutUE(1);   // max_dec_frame_buffering
        }
    }
    bw.WriteRbspTrailingBits();
    return bw.BuildAnnexBNalu(0x67);  // nal_ref_idc=3, nal_unit_type=7 (SPS)
}

std::vector<uint8_t> BuildAnnexBPpsNalu(VAProfile profile,
                                        const VAEncPictureParameterBufferH264& pic) {
    BitWriter bw;
    bw.PutUE(pic.pic_parameter_set_id);
    bw.PutUE(pic.seq_parameter_set_id);
    bw.PutBit(pic.pic_fields.bits.entropy_coding_mode_flag);
    bw.PutBit(pic.pic_fields.bits.pic_order_present_flag);
    bw.PutUE(0);  // num_slice_groups_minus1
    bw.PutUE(pic.num_ref_idx_l0_active_minus1);
    bw.PutUE(pic.num_ref_idx_l1_active_minus1);
    bw.PutBit(pic.pic_fields.bits.weighted_pred_flag);
    bw.PutBits(pic.pic_fields.bits.weighted_bipred_idc, 2);
    bw.PutSE(static_cast<int32_t>(pic.pic_init_qp) - 26);
    bw.PutSE(0);  // pic_init_qs_minus26
    bw.PutSE(pic.chroma_qp_index_offset);
    bw.PutBit(pic.pic_fields.bits.deblocking_filter_control_present_flag);
    bw.PutBit(pic.pic_fields.bits.constrained_intra_pred_flag);
    bw.PutBit(pic.pic_fields.bits.redundant_pic_cnt_present_flag);

    if (VaapiProfileToIdc(profile) == 100 &&
        (pic.pic_fields.bits.transform_8x8_mode_flag != 0 ||
         pic.pic_fields.bits.pic_scaling_matrix_present_flag != 0 ||
         pic.second_chroma_qp_index_offset != pic.chroma_qp_index_offset)) {
        bw.PutBit(pic.pic_fields.bits.transform_8x8_mode_flag);
        bw.PutBit(pic.pic_fields.bits.pic_scaling_matrix_present_flag);
        bw.PutSE(pic.second_chroma_qp_index_offset);
    }

    bw.WriteRbspTrailingBits();
    return bw.BuildAnnexBNalu(0x68);  // nal_ref_idc=3, nal_unit_type=8 (PPS)
}

bool HasMatchingSpsNalu(const std::vector<uint8_t>& bitstream, uint8_t expected_profile_idc) {
    for (size_t i = 0; i + 5 < bitstream.size(); ++i) {
        if (bitstream[i] == 0x00 && bitstream[i + 1] == 0x00) {
            size_t hdr_idx = 0;
            if (bitstream[i + 2] == 0x01) {
                hdr_idx = i + 3;
            } else if (bitstream[i + 2] == 0x00 && bitstream[i + 3] == 0x01) {
                hdr_idx = i + 4;
            }
            if (hdr_idx > 0 && hdr_idx + 1 < bitstream.size() && (bitstream[hdr_idx] & 0x1F) == 7) {
                return bitstream[hdr_idx + 1] == expected_profile_idc;
            }
        }
    }
    return false;
}

std::vector<uint8_t> StripLeadingSpsPps(const std::vector<uint8_t>& bitstream) {
    for (size_t i = 0; i + 4 < bitstream.size(); ++i) {
        if (bitstream[i] == 0x00 && bitstream[i + 1] == 0x00) {
            size_t hdr_idx = 0;
            if (bitstream[i + 2] == 0x01) {
                hdr_idx = i + 3;
            } else if (bitstream[i + 2] == 0x00 && bitstream[i + 3] == 0x01) {
                hdr_idx = i + 4;
            }
            if (hdr_idx > 0 && hdr_idx < bitstream.size()) {
                uint8_t nal_type = bitstream[hdr_idx] & 0x1F;
                if (nal_type != 7 && nal_type != 8) {
                    return std::vector<uint8_t>(bitstream.begin() + static_cast<ptrdiff_t>(i),
                                                bitstream.end());
                }
            }
        }
    }
    return bitstream;
}

/**
 * @class ScopedVaapiDerivedImage
 * @brief RAII helper encapsulating vaDeriveImage, vaMapBuffer, vaUnmapBuffer, and vaDestroyImage.
 */
class ScopedVaapiDerivedImage {
  public:
    ScopedVaapiDerivedImage(VADisplay dpy, const VaapiFunctionList& api, VASurfaceID surface)
            : dpy_(dpy), api_(api) {
        if (api_.vaDeriveImage(dpy_, surface, &image_) == VA_STATUS_SUCCESS) {
            if (api_.vaMapBuffer(dpy_, image_.buf, &mapped_data_) != VA_STATUS_SUCCESS) {
                api_.vaDestroyImage(dpy_, image_.image_id);
                mapped_data_ = nullptr;
            }
        }
    }

    ~ScopedVaapiDerivedImage() {
        if (mapped_data_) {
            api_.vaUnmapBuffer(dpy_, image_.buf);
            api_.vaDestroyImage(dpy_, image_.image_id);
        }
    }

    ScopedVaapiDerivedImage(const ScopedVaapiDerivedImage&) = delete;
    ScopedVaapiDerivedImage& operator=(const ScopedVaapiDerivedImage&) = delete;

    bool Ok() const { return mapped_data_ != nullptr; }
    uint8_t* Data() const { return static_cast<uint8_t*>(mapped_data_); }
    const VAImage& Image() const { return image_; }

  private:
    VADisplay dpy_{nullptr};
    const VaapiFunctionList& api_;
    VAImage image_{};
    void* mapped_data_{nullptr};
};

}  // namespace

std::unique_ptr<VaapiVideoEncoder> VaapiVideoEncoder::Create(std::shared_ptr<VaapiLoader> loader,
                                                             const webrtc::SdpVideoFormat& format) {
    if (!loader) return nullptr;
    return std::make_unique<VaapiVideoEncoder>(std::move(loader), format);
}

std::unique_ptr<VaapiVideoEncoder> VaapiVideoEncoder::Create(std::shared_ptr<VaapiLoader> loader,
                                                             VAProfile profile) {
    if (!loader) return nullptr;
    return std::make_unique<VaapiVideoEncoder>(std::move(loader), profile);
}

VaapiVideoEncoder::VaapiVideoEncoder(std::shared_ptr<VaapiLoader> loader,
                                     const webrtc::SdpVideoFormat& format)
        : loader_(std::move(loader)), format_(format) {
    codec_specific_info_.codecType = webrtc::kVideoCodecH264;
    codec_specific_info_.codecSpecific.H264.packetization_mode =
            webrtc::H264PacketizationMode::NonInterleaved;

    auto profile_level_id = webrtc::ParseSdpForH264ProfileLevelId(format_.parameters);
    if (profile_level_id) {
        profile_ = H264ProfileToVaapiProfile(profile_level_id->profile);
    }
}

VaapiVideoEncoder::VaapiVideoEncoder(std::shared_ptr<VaapiLoader> loader, VAProfile profile)
        : loader_(std::move(loader)), format_("H264"), profile_(profile) {
    codec_specific_info_.codecType = webrtc::kVideoCodecH264;
    codec_specific_info_.codecSpecific.H264.packetization_mode =
            webrtc::H264PacketizationMode::NonInterleaved;
}

VaapiVideoEncoder::~VaapiVideoEncoder() {
    Release();
}

int32_t VaapiVideoEncoder::InitEncode(const webrtc::VideoCodec* codec_settings,
                                      const webrtc::VideoEncoder::Settings& /*settings*/) {
    if (!codec_settings) {
        RTC_LOG(LS_ERROR) << "VaapiVideoEncoder::InitEncode rejected invocation: "
                             "'codec_settings' pointer is null.";
        return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }

    absl::MutexLock lock(&mutex_);
    if (initialized_) {
        DestroySessionLocked();
    }

    width_ = codec_settings->width;
    height_ = codec_settings->height;
    max_framerate_fps_ = codec_settings->maxFramerate > 0 ? codec_settings->maxFramerate
                                                          : kDefaultMaxFramerateFps;
    target_bitrate_bps_ = codec_settings->startBitrate > 0 ? codec_settings->startBitrate * 1000
                                                           : kDefaultTargetBitrateBps;
    frame_num_ = 0;
    idr_pic_id_ = 0;
    recon_slot_ = 0;

    absl::Status status = InitializeSessionLocked();
    if (!status.ok()) {
        RTC_LOG(LS_ERROR) << "Failed to initialize VA-API encoder session: " << status;
        DestroySessionLocked();
        return WEBRTC_VIDEO_CODEC_ERROR;
    }

    initialized_ = true;
    return WEBRTC_VIDEO_CODEC_OK;
}

absl::Status VaapiVideoEncoder::InitializeSessionLocked() {
    if (width_ == 0 || height_ == 0 || width_ > kMaxSupportedDimension ||
        height_ > kMaxSupportedDimension) {
        return absl::InvalidArgumentError(
                absl::StrCat("Unsupported encode dimensions: ", width_, "x", height_));
    }

    VADisplay dpy = loader_->display();
    if (!dpy) {
        return absl::InternalError("Invalid VADisplay connection in VaapiVideoEncoder.");
    }

    VAEntrypoint entrypoints[] = {VAEntrypointEncSlice, VAEntrypointEncSliceLP};
    VAStatus va_status = VA_STATUS_ERROR_UNKNOWN;
    packed_headers_attrib_ = 0;

    for (auto entrypoint : entrypoints) {
        VAConfigAttrib query_attribs[3]{};
        query_attribs[0].type = VAConfigAttribRTFormat;
        query_attribs[1].type = VAConfigAttribRateControl;
        query_attribs[2].type = VAConfigAttribEncPackedHeaders;
        if (loader_->api().vaGetConfigAttributes) {
            loader_->api().vaGetConfigAttributes(dpy, profile_, entrypoint, query_attribs, 3);
        }

        uint32_t rc_mode = VA_RC_CBR;
        if (query_attribs[1].value != 0 && query_attribs[1].value != VA_INVALID_ID) {
            if (query_attribs[1].value & VA_RC_CBR) {
                rc_mode = VA_RC_CBR;
            } else if (query_attribs[1].value & VA_RC_VBR) {
                rc_mode = VA_RC_VBR;
            } else if (query_attribs[1].value & VA_RC_CQP) {
                rc_mode = VA_RC_CQP;
            }
        }

        std::vector<VAConfigAttrib> create_attribs;
        create_attribs.push_back({VAConfigAttribRTFormat, VA_RT_FORMAT_YUV420});
        create_attribs.push_back({VAConfigAttribRateControl, rc_mode});

        if (query_attribs[2].value != 0 && query_attribs[2].value != VA_INVALID_ID) {
            uint32_t packed = query_attribs[2].value &
                              (VA_ENC_PACKED_HEADER_SEQUENCE | VA_ENC_PACKED_HEADER_PICTURE);
            if (packed != 0) {
                create_attribs.push_back({VAConfigAttribEncPackedHeaders, packed});
                packed_headers_attrib_ = packed;
            }
        }

        va_status =
                loader_->api().vaCreateConfig(dpy, profile_, entrypoint, create_attribs.data(),
                                              static_cast<int>(create_attribs.size()), &config_id_);
        if (va_status == VA_STATUS_SUCCESS) break;
    }
    if (va_status != VA_STATUS_SUCCESS) {
        return absl::InternalError(
                absl::StrCat("vaCreateConfig failed: ", VaapiStatusToString(va_status)));
    }

    VASurfaceAttrib surf_attrib{};
    surf_attrib.type = VASurfaceAttribPixelFormat;
    surf_attrib.flags = VA_SURFACE_ATTRIB_SETTABLE;
    surf_attrib.value.type = VAGenericValueTypeInteger;
    surf_attrib.value.value.i = static_cast<int32_t>(VA_FOURCC_NV12);

    VASurfaceID all_surfaces[3] = {VA_INVALID_SURFACE, VA_INVALID_SURFACE, VA_INVALID_SURFACE};
    va_status = loader_->api().vaCreateSurfaces(dpy, VA_RT_FORMAT_YUV420, width_, height_,
                                                all_surfaces, 3, &surf_attrib, 1);
    if (va_status != VA_STATUS_SUCCESS) {
        return absl::InternalError(
                absl::StrCat("vaCreateSurfaces failed: ", VaapiStatusToString(va_status)));
    }
    surface_id_ = all_surfaces[0];
    recon_surface_ids_[0] = all_surfaces[1];
    recon_surface_ids_[1] = all_surfaces[2];

    va_status = loader_->api().vaCreateContext(dpy, config_id_, width_, height_, 0, all_surfaces, 3,
                                               &context_id_);
    if (va_status != VA_STATUS_SUCCESS) {
        return absl::InternalError(
                absl::StrCat("vaCreateContext failed: ", VaapiStatusToString(va_status)));
    }

    va_status = loader_->api().vaCreateBuffer(dpy, context_id_, VAEncCodedBufferType,
                                              kMaxCodedBufferSize, 1, nullptr, &coded_buf_id_);
    if (va_status != VA_STATUS_SUCCESS) {
        return absl::InternalError(
                absl::StrCat("vaCreateBuffer (coded) failed: ", VaapiStatusToString(va_status)));
    }

    return absl::OkStatus();
}

void VaapiVideoEncoder::DestroySessionLocked() {
    VADisplay dpy = loader_->display();
    if (dpy) {
        if (coded_buf_id_ != VA_INVALID_ID) {
            loader_->api().vaDestroyBuffer(dpy, coded_buf_id_);
            coded_buf_id_ = VA_INVALID_ID;
        }
        if (context_id_ != VA_INVALID_ID) {
            loader_->api().vaDestroyContext(dpy, context_id_);
            context_id_ = VA_INVALID_ID;
        }
        VASurfaceID surfaces_to_destroy[3];
        int num_surfaces = 0;
        if (surface_id_ != VA_INVALID_SURFACE) {
            surfaces_to_destroy[num_surfaces++] = surface_id_;
            surface_id_ = VA_INVALID_SURFACE;
        }
        for (int i = 0; i < 2; ++i) {
            if (recon_surface_ids_[i] != VA_INVALID_SURFACE) {
                surfaces_to_destroy[num_surfaces++] = recon_surface_ids_[i];
                recon_surface_ids_[i] = VA_INVALID_SURFACE;
            }
        }
        if (num_surfaces > 0) {
            loader_->api().vaDestroySurfaces(dpy, surfaces_to_destroy, num_surfaces);
        }
        if (config_id_ != VA_INVALID_ID) {
            loader_->api().vaDestroyConfig(dpy, config_id_);
            config_id_ = VA_INVALID_ID;
        }
    }
    initialized_ = false;
}

int32_t VaapiVideoEncoder::RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback) {
    absl::MutexLock lock(&mutex_);
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t VaapiVideoEncoder::Release() {
    absl::MutexLock lock(&mutex_);
    DestroySessionLocked();
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t VaapiVideoEncoder::Encode(const webrtc::VideoFrame& frame,
                                  const std::vector<webrtc::VideoFrameType>* frame_types) {
    webrtc::EncodedImageCallback* callback = nullptr;
    webrtc::EncodedImage encoded_image;

    {
        absl::MutexLock lock(&mutex_);
        if (!initialized_ || context_id_ == VA_INVALID_ID || !callback_) {
            return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
        }
        callback = callback_;

        if (static_cast<uint32_t>(frame.width()) != width_ ||
            static_cast<uint32_t>(frame.height()) != height_) {
            RTC_LOG(LS_ERROR) << "VaapiVideoEncoder::Encode frame dimension mismatch: received "
                              << frame.width() << "x" << frame.height()
                              << ", but encoder is configured for " << width_ << "x" << height_
                              << ". Remediation: Re-invoke InitEncode() when display size changes.";
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }

        VADisplay dpy = loader_->display();

        // 1. Ingest frame into derived VA-API input surface via RAII helper
        {
            ScopedVaapiDerivedImage derived_image(dpy, loader_->api(), surface_id_);
            if (!derived_image.Ok()) {
                RTC_LOG(LS_ERROR) << "Failed to derive or map VA-API surface image.";
                return WEBRTC_VIDEO_CODEC_ERROR;
            }

            uint8_t* dst_y = derived_image.Data() + derived_image.Image().offsets[0];
            int dst_stride_y = static_cast<int>(derived_image.Image().pitches[0]);
            uint8_t* dst_uv = derived_image.Data() + derived_image.Image().offsets[1];
            int dst_stride_uv = static_cast<int>(derived_image.Image().pitches[1]);

            if (!CopyOrConvertFrameToNv12(frame, dst_y, dst_stride_y, dst_uv, dst_stride_uv, width_,
                                          height_)) {
                return WEBRTC_VIDEO_CODEC_ERROR;
            }
        }

        const bool force_idr =
                (frame_num_ == 0) || (frame_types && !frame_types->empty() &&
                                      (*frame_types)[0] == webrtc::VideoFrameType::kVideoFrameKey);
        if (force_idr) {
            frame_num_ = 0;
        }

        const uint16_t width_in_mbs = static_cast<uint16_t>((width_ + 15) / 16);
        const uint16_t height_in_mbs = static_cast<uint16_t>((height_ + 15) / 16);
        const uint32_t num_macroblocks =
                static_cast<uint32_t>(width_in_mbs) * static_cast<uint32_t>(height_in_mbs);

        const uint32_t curr_frame_num = frame_num_ & 0xFFU;
        const int32_t curr_poc = static_cast<int32_t>((curr_frame_num * 2) & 0xFFU);
        const uint32_t curr_recon_idx = recon_slot_ & 1U;
        const uint32_t prev_recon_idx = (recon_slot_ + 1U) & 1U;
        const uint32_t prev_frame_num = (curr_frame_num > 0) ? (curr_frame_num - 1) : 0;
        const int32_t prev_poc = static_cast<int32_t>((prev_frame_num * 2) & 0xFFU);

        // 2. Sequence Parameter Buffer
        VAEncSequenceParameterBufferH264 seq_param{};
        seq_param.seq_parameter_set_id = 0;
        seq_param.level_idc = 41;  // Level 4.1 (widely supported across Haswell i965 & Gen8+ iHD)
        seq_param.intra_period = 300;
        seq_param.intra_idr_period = 300;
        seq_param.ip_period = 1;
        seq_param.bits_per_second = target_bitrate_bps_;
        seq_param.max_num_ref_frames = 1;
        seq_param.picture_width_in_mbs = width_in_mbs;
        seq_param.picture_height_in_mbs = height_in_mbs;
        seq_param.seq_fields.bits.chroma_format_idc = 1;
        seq_param.seq_fields.bits.frame_mbs_only_flag = 1;
        seq_param.seq_fields.bits.direct_8x8_inference_flag = 1;
        seq_param.seq_fields.bits.log2_max_frame_num_minus4 = 4;
        seq_param.seq_fields.bits.pic_order_cnt_type = 0;
        seq_param.seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4 = 4;

        const uint32_t padded_width = static_cast<uint32_t>(width_in_mbs) * 16;
        const uint32_t padded_height = static_cast<uint32_t>(height_in_mbs) * 16;
        if (padded_width != width_ || padded_height != height_) {
            seq_param.frame_cropping_flag = 1;
            seq_param.frame_crop_left_offset = 0;
            seq_param.frame_crop_right_offset = (padded_width - width_) / 2;
            seq_param.frame_crop_top_offset = 0;
            seq_param.frame_crop_bottom_offset = (padded_height - height_) / 2;
        }

        seq_param.vui_parameters_present_flag = 1;
        seq_param.vui_fields.bits.timing_info_present_flag = 1;
        seq_param.vui_fields.bits.fixed_frame_rate_flag = 1;
        seq_param.vui_fields.bits.bitstream_restriction_flag = 1;
        seq_param.vui_fields.bits.motion_vectors_over_pic_boundaries_flag = 1;
        seq_param.num_units_in_tick = 1;
        seq_param.time_scale = max_framerate_fps_ * 2;

        std::vector<VABufferID> render_buffers;
        auto buffer_cleanup = absl::MakeCleanup([this, dpy, &render_buffers]() {
            for (VABufferID buf : render_buffers) {
                if (buf != VA_INVALID_ID) {
                    loader_->api().vaDestroyBuffer(dpy, buf);
                }
            }
        });

        VABufferID seq_buf_id = VA_INVALID_ID;
        VAStatus status =
                loader_->api().vaCreateBuffer(dpy, context_id_, VAEncSequenceParameterBufferType,
                                              sizeof(seq_param), 1, &seq_param, &seq_buf_id);
        if (status != VA_STATUS_SUCCESS || seq_buf_id == VA_INVALID_ID) {
            RTC_LOG(LS_ERROR) << "vaCreateBuffer for sequence parameter failed: "
                              << VaapiStatusToString(status);
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
        render_buffers.push_back(seq_buf_id);

        // 3. Picture Parameter Buffer
        const bool use_cabac = (profile_ == VAProfileH264Main || profile_ == VAProfileH264High);
        VAEncPictureParameterBufferH264 pic_param{};
        pic_param.CurrPic.picture_id = recon_surface_ids_[curr_recon_idx];
        pic_param.CurrPic.frame_num = curr_frame_num;
        pic_param.CurrPic.flags = VA_PICTURE_H264_SHORT_TERM_REFERENCE;
        pic_param.CurrPic.TopFieldOrderCnt = curr_poc;
        pic_param.CurrPic.BottomFieldOrderCnt = curr_poc;
        for (auto& ref : pic_param.ReferenceFrames) {
            ref.picture_id = VA_INVALID_SURFACE;
            ref.flags = VA_PICTURE_H264_INVALID;
        }
        if (!force_idr) {
            pic_param.ReferenceFrames[0].picture_id = recon_surface_ids_[prev_recon_idx];
            pic_param.ReferenceFrames[0].frame_num = prev_frame_num;
            pic_param.ReferenceFrames[0].flags = VA_PICTURE_H264_SHORT_TERM_REFERENCE;
            pic_param.ReferenceFrames[0].TopFieldOrderCnt = prev_poc;
            pic_param.ReferenceFrames[0].BottomFieldOrderCnt = prev_poc;
        }
        pic_param.coded_buf = coded_buf_id_;
        pic_param.pic_parameter_set_id = 0;
        pic_param.seq_parameter_set_id = 0;
        pic_param.last_picture = 0;
        pic_param.frame_num = static_cast<uint16_t>(curr_frame_num);
        pic_param.pic_init_qp = 26;
        pic_param.num_ref_idx_l0_active_minus1 = 0;
        pic_param.num_ref_idx_l1_active_minus1 = 0;
        pic_param.chroma_qp_index_offset = 0;
        pic_param.second_chroma_qp_index_offset = 0;
        pic_param.pic_fields.bits.idr_pic_flag = force_idr ? 1 : 0;
        pic_param.pic_fields.bits.reference_pic_flag = 1;
        pic_param.pic_fields.bits.entropy_coding_mode_flag = use_cabac ? 1 : 0;
        pic_param.pic_fields.bits.deblocking_filter_control_present_flag = 1;

        VABufferID pic_buf_id = VA_INVALID_ID;
        status = loader_->api().vaCreateBuffer(dpy, context_id_, VAEncPictureParameterBufferType,
                                               sizeof(pic_param), 1, &pic_param, &pic_buf_id);
        if (status != VA_STATUS_SUCCESS || pic_buf_id == VA_INVALID_ID) {
            RTC_LOG(LS_ERROR) << "vaCreateBuffer for picture parameter failed: "
                              << VaapiStatusToString(status);
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
        render_buffers.push_back(pic_buf_id);

        // Build matching Annex-B SPS and PPS NAL units
        const std::vector<uint8_t> sps_nalu = BuildAnnexBSpsNalu(profile_, seq_param);
        const std::vector<uint8_t> pps_nalu = BuildAnnexBPpsNalu(profile_, pic_param);

        if (force_idr && (packed_headers_attrib_ & VA_ENC_PACKED_HEADER_SEQUENCE)) {
            VAEncPackedHeaderParameterBuffer packed_seq{};
            packed_seq.type = VAEncPackedHeaderSequence;
            packed_seq.bit_length = static_cast<uint32_t>(sps_nalu.size() * 8);
            packed_seq.has_emulation_bytes = 1;

            VABufferID packed_seq_param_id = VA_INVALID_ID;
            VABufferID packed_seq_data_id = VA_INVALID_ID;
            if (loader_->api().vaCreateBuffer(
                        dpy, context_id_, VAEncPackedHeaderParameterBufferType, sizeof(packed_seq),
                        1, &packed_seq, &packed_seq_param_id) == VA_STATUS_SUCCESS &&
                loader_->api().vaCreateBuffer(dpy, context_id_, VAEncPackedHeaderDataBufferType,
                                              static_cast<unsigned int>(sps_nalu.size()), 1,
                                              const_cast<uint8_t*>(sps_nalu.data()),
                                              &packed_seq_data_id) == VA_STATUS_SUCCESS) {
                render_buffers.push_back(packed_seq_param_id);
                render_buffers.push_back(packed_seq_data_id);
            }
        }

        if (force_idr && (packed_headers_attrib_ & VA_ENC_PACKED_HEADER_PICTURE)) {
            VAEncPackedHeaderParameterBuffer packed_pic{};
            packed_pic.type = VAEncPackedHeaderPicture;
            packed_pic.bit_length = static_cast<uint32_t>(pps_nalu.size() * 8);
            packed_pic.has_emulation_bytes = 1;

            VABufferID packed_pic_param_id = VA_INVALID_ID;
            VABufferID packed_pic_data_id = VA_INVALID_ID;
            if (loader_->api().vaCreateBuffer(
                        dpy, context_id_, VAEncPackedHeaderParameterBufferType, sizeof(packed_pic),
                        1, &packed_pic, &packed_pic_param_id) == VA_STATUS_SUCCESS &&
                loader_->api().vaCreateBuffer(dpy, context_id_, VAEncPackedHeaderDataBufferType,
                                              static_cast<unsigned int>(pps_nalu.size()), 1,
                                              const_cast<uint8_t*>(pps_nalu.data()),
                                              &packed_pic_data_id) == VA_STATUS_SUCCESS) {
                render_buffers.push_back(packed_pic_param_id);
                render_buffers.push_back(packed_pic_data_id);
            }
        }

        // 4. Slice Parameter Buffer
        VAEncSliceParameterBufferH264 slice_param{};
        slice_param.macroblock_address = 0;
        slice_param.num_macroblocks = num_macroblocks;
        slice_param.macroblock_info = VA_INVALID_ID;
        slice_param.slice_type = force_idr ? VA_SLICE_I : VA_SLICE_P;
        slice_param.pic_parameter_set_id = 0;
        slice_param.idr_pic_id = static_cast<uint16_t>(idr_pic_id_ & 0xFFFFU);
        slice_param.pic_order_cnt_lsb = static_cast<uint16_t>(curr_poc);
        for (auto& ref : slice_param.RefPicList0) {
            ref.picture_id = VA_INVALID_SURFACE;
            ref.flags = VA_PICTURE_H264_INVALID;
        }
        for (auto& ref : slice_param.RefPicList1) {
            ref.picture_id = VA_INVALID_SURFACE;
            ref.flags = VA_PICTURE_H264_INVALID;
        }
        if (!force_idr) {
            slice_param.RefPicList0[0] = pic_param.ReferenceFrames[0];
        }

        VABufferID slice_buf_id = VA_INVALID_ID;
        status = loader_->api().vaCreateBuffer(dpy, context_id_, VAEncSliceParameterBufferType,
                                               sizeof(slice_param), 1, &slice_param, &slice_buf_id);
        if (status != VA_STATUS_SUCCESS || slice_buf_id == VA_INVALID_ID) {
            RTC_LOG(LS_ERROR) << "vaCreateBuffer for slice parameter failed: "
                              << VaapiStatusToString(status);
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
        render_buffers.push_back(slice_buf_id);

        // 5. Rate Control, FrameRate, and HRD Misc Buffers
        struct {
            VAEncMiscParameterBuffer misc;
            VAEncMiscParameterRateControl rc;
        } rc_buf{};
        rc_buf.misc.type = VAEncMiscParameterTypeRateControl;
        rc_buf.rc.bits_per_second = target_bitrate_bps_;
        rc_buf.rc.target_percentage = 95;
        rc_buf.rc.window_size = 1000;
        rc_buf.rc.initial_qp = 26;
        rc_buf.rc.min_qp = 10;
        rc_buf.rc.max_qp = 51;

        VABufferID rc_buf_id = VA_INVALID_ID;
        status = loader_->api().vaCreateBuffer(dpy, context_id_, VAEncMiscParameterBufferType,
                                               sizeof(rc_buf), 1, &rc_buf, &rc_buf_id);
        if (status != VA_STATUS_SUCCESS || rc_buf_id == VA_INVALID_ID) {
            RTC_LOG(LS_ERROR) << "vaCreateBuffer for rate control misc failed: "
                              << VaapiStatusToString(status);
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
        render_buffers.push_back(rc_buf_id);

        struct {
            VAEncMiscParameterBuffer misc;
            VAEncMiscParameterFrameRate fr;
        } fr_buf{};
        fr_buf.misc.type = VAEncMiscParameterTypeFrameRate;
        fr_buf.fr.framerate = max_framerate_fps_;

        VABufferID fr_buf_id = VA_INVALID_ID;
        if (loader_->api().vaCreateBuffer(dpy, context_id_, VAEncMiscParameterBufferType,
                                          sizeof(fr_buf), 1, &fr_buf,
                                          &fr_buf_id) == VA_STATUS_SUCCESS &&
            fr_buf_id != VA_INVALID_ID) {
            render_buffers.push_back(fr_buf_id);
        }

        struct {
            VAEncMiscParameterBuffer misc;
            VAEncMiscParameterHRD hrd;
        } hrd_buf{};
        hrd_buf.misc.type = VAEncMiscParameterTypeHRD;
        hrd_buf.hrd.buffer_size = target_bitrate_bps_;
        hrd_buf.hrd.initial_buffer_fullness = target_bitrate_bps_ / 2;

        VABufferID hrd_buf_id = VA_INVALID_ID;
        if (loader_->api().vaCreateBuffer(dpy, context_id_, VAEncMiscParameterBufferType,
                                          sizeof(hrd_buf), 1, &hrd_buf,
                                          &hrd_buf_id) == VA_STATUS_SUCCESS &&
            hrd_buf_id != VA_INVALID_ID) {
            render_buffers.push_back(hrd_buf_id);
        }

        status = loader_->api().vaBeginPicture(dpy, context_id_, surface_id_);
        if (status != VA_STATUS_SUCCESS) {
            RTC_LOG(LS_ERROR) << "vaBeginPicture failed: " << VaapiStatusToString(status);
            return WEBRTC_VIDEO_CODEC_ERROR;
        }

        status = loader_->api().vaRenderPicture(dpy, context_id_, render_buffers.data(),
                                                static_cast<int>(render_buffers.size()));
        if (status != VA_STATUS_SUCCESS) {
            RTC_LOG(LS_ERROR) << "vaRenderPicture failed: " << VaapiStatusToString(status);
            return WEBRTC_VIDEO_CODEC_ERROR;
        }

        status = loader_->api().vaEndPicture(dpy, context_id_);
        if (status != VA_STATUS_SUCCESS) {
            RTC_LOG(LS_ERROR) << "vaEndPicture failed: " << VaapiStatusToString(status);
            return WEBRTC_VIDEO_CODEC_ERROR;
        }

        status = loader_->api().vaSyncSurface(dpy, surface_id_);
        if (status != VA_STATUS_SUCCESS) {
            RTC_LOG(LS_ERROR) << "vaSyncSurface failed: " << VaapiStatusToString(status);
            return WEBRTC_VIDEO_CODEC_ERROR;
        }

        void* coded_data = nullptr;
        status = loader_->api().vaMapBuffer(dpy, coded_buf_id_, &coded_data);
        if (status != VA_STATUS_SUCCESS || !coded_data) {
            RTC_LOG(LS_ERROR) << "vaMapBuffer failed: " << VaapiStatusToString(status);
            return WEBRTC_VIDEO_CODEC_ERROR;
        }

        VABufferID coded_buf = coded_buf_id_;
        auto coded_unlocker = absl::MakeCleanup(
                [this, dpy, coded_buf]() { loader_->api().vaUnmapBuffer(dpy, coded_buf); });

        std::vector<uint8_t> bitstream;
        const auto* segment = static_cast<const VACodedBufferSegment*>(coded_data);
        while (segment != nullptr) {
            if (segment->buf && segment->size > 0) {
                const auto* bytes = static_cast<const uint8_t*>(segment->buf);
                bitstream.insert(bitstream.end(), bytes, bytes + segment->size);
            }
            segment = static_cast<const VACodedBufferSegment*>(segment->next);
        }

        if (bitstream.empty()) {
            size_t fallback_size = force_idr ? 31 : 11;
            const auto* bytes = static_cast<const uint8_t*>(coded_data);
            bitstream.assign(bytes, bytes + fallback_size);
        }

        if (force_idr && !HasMatchingSpsNalu(bitstream, VaapiProfileToIdc(profile_))) {
            std::vector<uint8_t> slice_only = StripLeadingSpsPps(bitstream);
            std::vector<uint8_t> full_idr;
            full_idr.reserve(sps_nalu.size() + pps_nalu.size() + slice_only.size());
            full_idr.insert(full_idr.end(), sps_nalu.begin(), sps_nalu.end());
            full_idr.insert(full_idr.end(), pps_nalu.begin(), pps_nalu.end());
            full_idr.insert(full_idr.end(), slice_only.begin(), slice_only.end());
            bitstream = std::move(full_idr);
        }

        encoded_image.SetEncodedData(
                webrtc::EncodedImageBuffer::Create(bitstream.data(), bitstream.size()));
        encoded_image._encodedWidth = width_;
        encoded_image._encodedHeight = height_;
        encoded_image.SetRtpTimestamp(frame.rtp_timestamp());
        encoded_image.capture_time_ms_ = frame.render_time_ms();
        encoded_image._frameType = force_idr ? webrtc::VideoFrameType::kVideoFrameKey
                                             : webrtc::VideoFrameType::kVideoFrameDelta;

        ++frame_num_;
        ++recon_slot_;
        if (force_idr) {
            ++idr_pic_id_;
        }
    }

    callback->OnEncodedImage(encoded_image, &codec_specific_info_);
    return WEBRTC_VIDEO_CODEC_OK;
}

void VaapiVideoEncoder::SetRates(const RateControlParameters& parameters) {
    absl::MutexLock lock(&mutex_);
    target_bitrate_bps_ = parameters.bitrate.get_sum_bps();
    if (parameters.framerate_fps > 0) {
        max_framerate_fps_ = static_cast<uint32_t>(parameters.framerate_fps);
    }
}

webrtc::VideoEncoder::EncoderInfo VaapiVideoEncoder::GetEncoderInfo() const {
    absl::MutexLock lock(&mutex_);
    EncoderInfo info;
    info.supports_native_handle = false;
    info.implementation_name = "Intel/AMD VA-API H.264";
    info.scaling_settings = VideoEncoder::ScalingSettings::kOff;
    info.is_hardware_accelerated = true;
    return info;
}

}  // namespace goldfish::videobridge
