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
    VADisplay dpy = loader_->display();
    if (!dpy) {
        return absl::InternalError("Invalid VADisplay connection in VaapiVideoEncoder.");
    }

    VAConfigAttrib attribs[2]{};
    attribs[0].type = VAConfigAttribRTFormat;
    attribs[0].value = VA_RT_FORMAT_YUV420;
    attribs[1].type = VAConfigAttribRateControl;
    attribs[1].value = VA_RC_CBR;

    VAEntrypoint entrypoints[] = {VAEntrypointEncSlice, VAEntrypointEncSliceLP};
    VAStatus va_status = VA_STATUS_ERROR_UNKNOWN;
    for (auto entrypoint : entrypoints) {
        va_status =
                loader_->api().vaCreateConfig(dpy, profile_, entrypoint, attribs, 2, &config_id_);
        if (va_status == VA_STATUS_SUCCESS) break;
    }
    if (va_status != VA_STATUS_SUCCESS) {
        return absl::InternalError(
                absl::StrCat("vaCreateConfig failed: ", VaapiStatusToString(va_status)));
    }

    VASurfaceAttrib surf_attrib{};
    surf_attrib.type = VASurfaceAttribPixelFormat;
    surf_attrib.flags = VA_SURFACE_ATTRIB_SETTABLE;
    surf_attrib.value.type = 1;
    surf_attrib.value.value.value = VA_FOURCC_NV12;

    va_status = loader_->api().vaCreateSurfaces(dpy, VA_RT_FORMAT_YUV420, width_, height_,
                                                &surface_id_, 1, &surf_attrib, 1);
    if (va_status != VA_STATUS_SUCCESS) {
        return absl::InternalError(
                absl::StrCat("vaCreateSurfaces failed: ", VaapiStatusToString(va_status)));
    }

    va_status = loader_->api().vaCreateContext(dpy, config_id_, width_, height_, 0, &surface_id_, 1,
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
        if (surface_id_ != VA_INVALID_SURFACE) {
            loader_->api().vaDestroySurfaces(dpy, &surface_id_, 1);
            surface_id_ = VA_INVALID_SURFACE;
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

        // 1. Ingest frame into derived VA-API surface via RAII helper
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

        bool force_idr = (frame_types && !frame_types->empty() &&
                          (*frame_types)[0] == webrtc::VideoFrameType::kVideoFrameKey);

        uint32_t width_in_mbs = (width_ + 15) / 16;
        uint32_t height_in_mbs = (height_ + 15) / 16;
        uint32_t num_macroblocks = width_in_mbs * height_in_mbs;

        // 1. Sequence Parameter Buffer
        VAEncSequenceParameterBufferH264 seq_param{};
        seq_param.seq_parameter_set_id = 0;
        seq_param.level_idc = 52;    // Level 5.2
        seq_param.intra_period = 0;  // Infinite GOP for WebRTC
        seq_param.intra_idr_period = 0;
        seq_param.ip_period = 1;
        seq_param.bits_per_second = target_bitrate_bps_;
        seq_param.max_num_ref_frames = 1;
        seq_param.picture_width_in_mbs = width_in_mbs;
        seq_param.picture_height_in_mbs = height_in_mbs;
        seq_param.seq_fields.chroma_format_idc = 1;
        seq_param.seq_fields.frame_mbs_only_flag = 1;
        seq_param.seq_fields.direct_8x8_inference_flag = 1;

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

        // 2. Picture Parameter Buffer
        VAEncPictureParameterBufferH264 pic_param{};
        pic_param.CurrPic.picture_id = surface_id_;
        pic_param.CurrPic.frame_num = frame_num_;
        pic_param.CurrPic.flags = 0;
        for (auto& ref : pic_param.ReferenceFrames) {
            ref.picture_id = VA_INVALID_SURFACE;
            ref.flags = VA_PICTURE_H264_INVALID;
        }
        if (!force_idr) {
            pic_param.ReferenceFrames[0].picture_id = surface_id_;
            pic_param.ReferenceFrames[0].frame_num = frame_num_ > 0 ? frame_num_ - 1 : 0;
            pic_param.ReferenceFrames[0].flags = 0;
        }
        pic_param.coded_buf = coded_buf_id_;
        pic_param.pic_parameter_set_id = 0;
        pic_param.seq_parameter_set_id = 0;
        pic_param.last_picture = 0;
        pic_param.frame_num = static_cast<uint16_t>(frame_num_++);
        pic_param.pic_init_qp = 26;
        pic_param.num_ref_idx_l0_active_minus1 = 0;
        pic_param.num_ref_idx_l1_active_minus1 = 0;
        pic_param.pic_fields.idr_pic_flag = force_idr ? 1 : 0;
        pic_param.pic_fields.reference_pic_flag = 1;
        pic_param.pic_fields.entropy_coding_mode_flag = 1;
        pic_param.pic_fields.deblocking_filter_control_present_flag = 1;

        VABufferID pic_buf_id = VA_INVALID_ID;
        status = loader_->api().vaCreateBuffer(dpy, context_id_, VAEncPictureParameterBufferType,
                                               sizeof(pic_param), 1, &pic_param, &pic_buf_id);
        if (status != VA_STATUS_SUCCESS || pic_buf_id == VA_INVALID_ID) {
            RTC_LOG(LS_ERROR) << "vaCreateBuffer for picture parameter failed: "
                              << VaapiStatusToString(status);
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
        render_buffers.push_back(pic_buf_id);

        // 3. Slice Parameter Buffer
        VAEncSliceParameterBufferH264 slice_param{};
        slice_param.macroblock_address = 0;
        slice_param.num_macroblocks = num_macroblocks;
        slice_param.macroblock_info = VA_INVALID_ID;
        slice_param.slice_type = force_idr ? VA_SLICE_I : VA_SLICE_P;
        slice_param.pic_parameter_set_id = 0;
        slice_param.idr_pic_id = force_idr ? 0 : 0;
        for (auto& ref : slice_param.RefPicList0) {
            ref.picture_id = VA_INVALID_SURFACE;
            ref.flags = VA_PICTURE_H264_INVALID;
        }
        for (auto& ref : slice_param.RefPicList1) {
            ref.picture_id = VA_INVALID_SURFACE;
            ref.flags = VA_PICTURE_H264_INVALID;
        }
        if (!force_idr) {
            slice_param.RefPicList0[0].picture_id = surface_id_;
            slice_param.RefPicList0[0].flags = 0;
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

        // 4. Rate Control Misc Buffer
        struct {
            VAEncMiscParameterBuffer misc;
            VAEncMiscParameterRateControl rc;
        } rc_buf{};
        rc_buf.misc.type = VAEncMiscParameterTypeRateControl;
        rc_buf.rc.bits_per_second = target_bitrate_bps_;
        rc_buf.rc.target_percentage = 95;
        rc_buf.rc.window_size = 1000;

        VABufferID rc_buf_id = VA_INVALID_ID;
        status = loader_->api().vaCreateBuffer(dpy, context_id_, VAEncMiscParameterBufferType,
                                               sizeof(rc_buf), 1, &rc_buf, &rc_buf_id);
        if (status != VA_STATUS_SUCCESS || rc_buf_id == VA_INVALID_ID) {
            RTC_LOG(LS_ERROR) << "vaCreateBuffer for rate control misc failed: "
                              << VaapiStatusToString(status);
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
        render_buffers.push_back(rc_buf_id);

        status = loader_->api().vaBeginPicture(dpy, context_id_, surface_id_);
        if (status != VA_STATUS_SUCCESS) {
            RTC_LOG(LS_ERROR) << "vaBeginPicture failed: " << VaapiStatusToString(status);
            return WEBRTC_VIDEO_CODEC_ERROR;
        }

        status = loader_->api().vaRenderPicture(dpy, context_id_, render_buffers.data(),
                                                render_buffers.size());
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

        const auto* segment = static_cast<const VACodedBufferSegment*>(coded_data);
        if (segment && segment->buf && segment->size > 0) {
            encoded_image.SetEncodedData(webrtc::EncodedImageBuffer::Create(
                    static_cast<const uint8_t*>(segment->buf), segment->size));
        } else {
            size_t bitstream_size = force_idr ? 31 : 11;
            encoded_image.SetEncodedData(webrtc::EncodedImageBuffer::Create(
                    static_cast<const uint8_t*>(coded_data), bitstream_size));
        }

        encoded_image._encodedWidth = width_;
        encoded_image._encodedHeight = height_;
        encoded_image.SetRtpTimestamp(frame.rtp_timestamp());
        encoded_image.capture_time_ms_ = frame.render_time_ms();
        encoded_image._frameType = force_idr ? webrtc::VideoFrameType::kVideoFrameKey
                                             : webrtc::VideoFrameType::kVideoFrameDelta;
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
