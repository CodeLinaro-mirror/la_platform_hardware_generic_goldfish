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
#include "core/linux/nvenc_video_encoder.h"

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
#include "api/video/video_frame_buffer.h"
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
}  // namespace

std::unique_ptr<NvencVideoEncoder> NvencVideoEncoder::Create(std::shared_ptr<NvencLoader> loader,
                                                             const webrtc::SdpVideoFormat& format) {
    if (!loader) return nullptr;
    return std::make_unique<NvencVideoEncoder>(std::move(loader), format);
}

NvencVideoEncoder::NvencVideoEncoder(std::shared_ptr<NvencLoader> loader,
                                     const webrtc::SdpVideoFormat& format)
        : loader_(std::move(loader)), format_(format) {
    codec_specific_info_.codecType = webrtc::kVideoCodecH264;
    codec_specific_info_.codecSpecific.H264.packetization_mode =
            webrtc::H264PacketizationMode::NonInterleaved;

    auto profile_level_id = webrtc::ParseSdpForH264ProfileLevelId(format_.parameters);
    if (profile_level_id) {
        profile_guid_ = H264ProfileToNvencGuid(profile_level_id->profile);
    }
}

NvencVideoEncoder::~NvencVideoEncoder() {
    Release();
}

int32_t NvencVideoEncoder::InitEncode(const webrtc::VideoCodec* codec_settings,
                                      const webrtc::VideoEncoder::Settings& /*settings*/) {
    if (!codec_settings) {
        RTC_LOG(LS_ERROR) << "NvencVideoEncoder::InitEncode rejected invocation: "
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

    absl::Status status = InitializeSessionLocked();
    if (!status.ok()) {
        RTC_LOG(LS_ERROR) << "Failed to initialize NVENC session: " << status;
        DestroySessionLocked();
        return WEBRTC_VIDEO_CODEC_ERROR;
    }

    initialized_ = true;
    return WEBRTC_VIDEO_CODEC_OK;
}

absl::Status NvencVideoEncoder::InitializeSessionLocked() {
    auto session_params = MakeNvencStruct<NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS>();
    session_params.apiVersion = NVENCAPI_VERSION;
    session_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;

    NVENCSTATUS status =
            loader_->api().nvEncOpenEncodeSessionEx(&session_params, &encoder_session_);
    if (status != NV_ENC_SUCCESS || !encoder_session_) {
        return absl::InternalError(absl::StrCat(
                "nvEncOpenEncodeSessionEx failed: ", NvencStatusToString(status),
                ". Remediation: Verify an NVIDIA GPU (Kepler+) is present, the CUDA driver "
                "(libcuda.so.1) is installed, and the max concurrent NVENC session limit is not "
                "exceeded."));
    }

    auto preset_config = MakeNvencStruct<NV_ENC_PRESET_CONFIG>();
    preset_config.presetCfg = MakeNvencStruct<NV_ENC_CONFIG>();

    status = loader_->api().nvEncGetEncodePresetConfig(encoder_session_, NV_ENC_CODEC_H264_GUID,
                                                       NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID,
                                                       &preset_config);
    if (status != NV_ENC_SUCCESS) {
        return absl::InternalError(
                absl::StrCat("nvEncGetEncodePresetConfig failed: ", NvencStatusToString(status)));
    }

    // Configure NVENC for low-latency, real-time WebRTC streaming:
    encode_config_ = preset_config.presetCfg;
    encode_config_.version = NV_ENC_CONFIG_VER;

    // Profile: Configured according to SDP-negotiated H.264 profile (Baseline, Main, or High).
    encode_config_.profileGUID = profile_guid_;

    // Infinite GOP: WebRTC does not use periodic keyframes (which spike network bandwidth).
    // Instead, keyframes (IDR) are generated on-demand when a PLI (Picture Loss Indication)
    // arrives.
    encode_config_.gopLength = NVENC_INFINITE_GOPLENGTH;
    encode_config_.encodeCodecConfig.h264Config.idrPeriod = NVENC_INFINITE_GOPLENGTH;

    // Zero B-Frames: IPPP structure eliminates frame reordering and display latency.
    encode_config_.frameIntervalP = 1;

    // Rate Control: CBR (Constant Bitrate) with spatial Adaptive Quantization (AQ)
    // for smooth bitrate pacing and crisp text/UI rendering in emulator displays.
    encode_config_.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    encode_config_.rcParams.averageBitRate = target_bitrate_bps_;
    encode_config_.rcParams.maxBitRate = target_bitrate_bps_;
    encode_config_.rcParams.enableAQ = 1;

    auto init_params = MakeNvencStruct<NV_ENC_INITIALIZE_PARAMS>();
    init_params.encodeGUID = NV_ENC_CODEC_H264_GUID;
    init_params.presetGUID = NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID;
    init_params.encodeWidth = width_;
    init_params.encodeHeight = height_;
    init_params.darWidth = width_;
    init_params.darHeight = height_;
    init_params.frameRateNum = max_framerate_fps_;
    init_params.frameRateDen = 1;
    init_params.enablePTD = 1;  // Picture Type Decision handled by GPU hardware
    init_params.encodeConfig = &encode_config_;

    status = loader_->api().nvEncInitializeEncoder(encoder_session_, &init_params);
    if (status != NV_ENC_SUCCESS) {
        return absl::InternalError(
                absl::StrCat("nvEncInitializeEncoder failed: ", NvencStatusToString(status),
                             " (resolution: ", width_, "x", height_, ", fps: ", max_framerate_fps_,
                             ", bitrate: ", target_bitrate_bps_,
                             " bps). Remediation: Verify resolution is within GPU limits and "
                             "dimensions are even integers."));
    }

    auto input_params = MakeNvencStruct<NV_ENC_CREATE_INPUT_BUFFER>();
    input_params.width = width_;
    input_params.height = height_;
    input_params.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;

    status = loader_->api().nvEncCreateInputBuffer(encoder_session_, &input_params);
    if (status != NV_ENC_SUCCESS) {
        return absl::InternalError(
                absl::StrCat("nvEncCreateInputBuffer failed: ", NvencStatusToString(status)));
    }
    input_buffer_ = input_params.inputBuffer;

    auto bitstream_params = MakeNvencStruct<NV_ENC_CREATE_BITSTREAM_BUFFER>();

    status = loader_->api().nvEncCreateBitstreamBuffer(encoder_session_, &bitstream_params);
    if (status != NV_ENC_SUCCESS) {
        return absl::InternalError(
                absl::StrCat("nvEncCreateBitstreamBuffer failed: ", NvencStatusToString(status)));
    }
    bitstream_buffer_ = bitstream_params.bitstreamBuffer;

    return absl::OkStatus();
}

void NvencVideoEncoder::DestroySessionLocked() {
    if (encoder_session_) {
        if (input_buffer_) {
            loader_->api().nvEncDestroyInputBuffer(encoder_session_, input_buffer_);
            input_buffer_ = nullptr;
        }
        if (bitstream_buffer_) {
            loader_->api().nvEncDestroyBitstreamBuffer(encoder_session_, bitstream_buffer_);
            bitstream_buffer_ = nullptr;
        }
        loader_->api().nvEncDestroyEncoder(encoder_session_);
        encoder_session_ = nullptr;
    }
    initialized_ = false;
}

int32_t NvencVideoEncoder::RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback) {
    absl::MutexLock lock(&mutex_);
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t NvencVideoEncoder::Release() {
    absl::MutexLock lock(&mutex_);
    DestroySessionLocked();
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t NvencVideoEncoder::Encode(const webrtc::VideoFrame& frame,
                                  const std::vector<webrtc::VideoFrameType>* frame_types) {
    webrtc::EncodedImageCallback* callback = nullptr;
    webrtc::EncodedImage encoded_image;

    {
        absl::MutexLock lock(&mutex_);
        if (!initialized_ || !encoder_session_ || !callback_) {
            return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
        }
        callback = callback_;

        if (frame.width() != width_ || frame.height() != height_) {
            RTC_LOG(LS_ERROR) << "NvencVideoEncoder::Encode frame dimension mismatch: received "
                              << frame.width() << "x" << frame.height()
                              << ", but encoder is configured for " << width_ << "x" << height_
                              << ". Remediation: Re-invoke InitEncode() when display size changes.";
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }

        // Lock input buffer for CPU upload
        auto lock_input_params = MakeNvencStruct<NV_ENC_LOCK_INPUT_BUFFER>();
        lock_input_params.inputBuffer = input_buffer_;

        NVENCSTATUS status =
                loader_->api().nvEncLockInputBuffer(encoder_session_, &lock_input_params);
        if (status != NV_ENC_SUCCESS) {
            RTC_LOG(LS_ERROR) << "nvEncLockInputBuffer failed: " << NvencStatusToString(status);
            return WEBRTC_VIDEO_CODEC_ERROR;
        }

        void* session = encoder_session_;
        NV_ENC_INPUT_PTR input_buf = input_buffer_;
        auto input_unlocker = absl::MakeCleanup([this, session, input_buf]() {
            loader_->api().nvEncUnlockInputBuffer(session, input_buf);
        });

        uint8_t* dst_y = static_cast<uint8_t*>(lock_input_params.bufferDataPtr);
        int dst_stride_y = static_cast<int>(lock_input_params.pitch);
        uint8_t* dst_uv = dst_y + static_cast<ptrdiff_t>(dst_stride_y) * height_;
        int dst_stride_uv = dst_stride_y;

        if (!CopyOrConvertFrameToNv12(frame, dst_y, dst_stride_y, dst_uv, dst_stride_uv, width_,
                                      height_)) {
            return WEBRTC_VIDEO_CODEC_ERROR;
        }

        std::move(input_unlocker).Invoke();

        bool force_idr = (frame_types && !frame_types->empty() &&
                          (*frame_types)[0] == webrtc::VideoFrameType::kVideoFrameKey);

        auto encode_pic_params = MakeNvencStruct<NV_ENC_PIC_PARAMS>();
        encode_pic_params.inputBuffer = input_buffer_;
        encode_pic_params.outputBitstream = bitstream_buffer_;
        encode_pic_params.inputWidth = width_;
        encode_pic_params.inputHeight = height_;
        encode_pic_params.inputPitch = dst_stride_y;
        encode_pic_params.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
        encode_pic_params.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
        if (force_idr) {
            encode_pic_params.encodePicFlags |= NV_ENC_PIC_FLAG_FORCEIDR;
        }

        status = loader_->api().nvEncEncodePicture(encoder_session_, &encode_pic_params);
        if (status != NV_ENC_SUCCESS) {
            RTC_LOG(LS_ERROR) << "nvEncEncodePicture failed: " << NvencStatusToString(status);
            return WEBRTC_VIDEO_CODEC_ERROR;
        }

        auto lock_bitstream_params = MakeNvencStruct<NV_ENC_LOCK_BITSTREAM>();
        lock_bitstream_params.outputBitstream = bitstream_buffer_;

        status = loader_->api().nvEncLockBitstream(encoder_session_, &lock_bitstream_params);
        if (status != NV_ENC_SUCCESS) {
            RTC_LOG(LS_ERROR) << "nvEncLockBitstream failed: " << NvencStatusToString(status);
            return WEBRTC_VIDEO_CODEC_ERROR;
        }

        auto bitstream_unlocker =
                absl::MakeCleanup([this, session = encoder_session_, buf = bitstream_buffer_]() {
                    loader_->api().nvEncUnlockBitstream(session, buf);
                });

        encoded_image.SetEncodedData(webrtc::EncodedImageBuffer::Create(
                static_cast<const uint8_t*>(lock_bitstream_params.bitstreamBufferPtr),
                lock_bitstream_params.bitstreamSizeInBytes));
        encoded_image._encodedWidth = width_;
        encoded_image._encodedHeight = height_;
        encoded_image.SetRtpTimestamp(frame.rtp_timestamp());
        encoded_image.capture_time_ms_ = frame.render_time_ms();
        encoded_image._frameType = (lock_bitstream_params.pictureType == NV_ENC_PIC_TYPE_IDR)
                                           ? webrtc::VideoFrameType::kVideoFrameKey
                                           : webrtc::VideoFrameType::kVideoFrameDelta;
    }

    callback->OnEncodedImage(encoded_image, &codec_specific_info_);
    return WEBRTC_VIDEO_CODEC_OK;
}

void NvencVideoEncoder::SetRates(const RateControlParameters& parameters) {
    absl::MutexLock lock(&mutex_);
    target_bitrate_bps_ = parameters.bitrate.get_sum_bps();
    if (parameters.framerate_fps > 0) {
        max_framerate_fps_ = static_cast<uint32_t>(parameters.framerate_fps);
    }

    if (!initialized_ || !encoder_session_) return;

    encode_config_.rcParams.averageBitRate = target_bitrate_bps_;
    encode_config_.rcParams.maxBitRate = target_bitrate_bps_;

    auto reconfig_params = MakeNvencStruct<NV_ENC_RECONFIGURE_PARAMS>();
    reconfig_params.reInitEncodeParams = MakeNvencStruct<NV_ENC_INITIALIZE_PARAMS>();
    reconfig_params.reInitEncodeParams.encodeGUID = NV_ENC_CODEC_H264_GUID;
    reconfig_params.reInitEncodeParams.presetGUID = NV_ENC_PRESET_LOW_LATENCY_DEFAULT_GUID;
    reconfig_params.reInitEncodeParams.encodeWidth = width_;
    reconfig_params.reInitEncodeParams.encodeHeight = height_;
    reconfig_params.reInitEncodeParams.darWidth = width_;
    reconfig_params.reInitEncodeParams.darHeight = height_;
    reconfig_params.reInitEncodeParams.frameRateNum = max_framerate_fps_;
    reconfig_params.reInitEncodeParams.frameRateDen = 1;
    reconfig_params.reInitEncodeParams.enablePTD = 1;
    reconfig_params.reInitEncodeParams.encodeConfig = &encode_config_;

    NVENCSTATUS status = loader_->api().nvEncReconfigureEncoder(encoder_session_, &reconfig_params);
    if (status != NV_ENC_SUCCESS) {
        RTC_LOG(LS_WARNING) << "nvEncReconfigureEncoder failed: " << NvencStatusToString(status)
                            << " (target bitrate: " << target_bitrate_bps_
                            << " bps, fps: " << max_framerate_fps_ << ").";
    }
}

webrtc::VideoEncoder::EncoderInfo NvencVideoEncoder::GetEncoderInfo() const {
    absl::MutexLock lock(&mutex_);
    EncoderInfo info;
    info.supports_native_handle = false;
    info.implementation_name = "NVIDIA NVENC H.264";
    info.scaling_settings = VideoEncoder::ScalingSettings::kOff;
    info.is_hardware_accelerated = true;
    return info;
}

}  // namespace goldfish::videobridge
