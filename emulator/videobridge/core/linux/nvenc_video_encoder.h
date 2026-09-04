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
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "api/video_codecs/h264_profile_level_id.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"
#include "modules/video_coding/include/video_codec_interface.h"
#pragma clang diagnostic pop

#include <nvEncodeAPI.h>

#include "core/linux/nvenc_loader.h"

namespace goldfish::videobridge {

/**
 * @brief Maps WebRTC H264Profile enum to the corresponding NVIDIA NVENC profile GUID.
 */
inline GUID H264ProfileToNvencGuid(webrtc::H264Profile profile) {
    switch (profile) {
    case webrtc::H264Profile::kProfileMain:
        return NV_ENC_H264_PROFILE_MAIN_GUID;
    case webrtc::H264Profile::kProfileHigh:
    case webrtc::H264Profile::kProfileConstrainedHigh:
        return NV_ENC_H264_PROFILE_HIGH_GUID;
    case webrtc::H264Profile::kProfileConstrainedBaseline:
    case webrtc::H264Profile::kProfileBaseline:
    default:
        return NV_ENC_H264_PROFILE_BASELINE_GUID;
    }
}

/**
 * @class NvencVideoEncoder
 * @brief Hardware-accelerated H.264 video encoder using NVIDIA NVENC.
 *
 * Ingests CPU-accessible I420/NV12 frame buffers, uploads to mapped NVENC input
 * memory buffers, executes GPU fixed-function encoding, and emits Annex B H.264
 * NAL units through the registered @c webrtc::EncodedImageCallback.
 */
class NvencVideoEncoder : public webrtc::VideoEncoder {
  public:
    static std::unique_ptr<NvencVideoEncoder> Create(
            std::shared_ptr<NvencLoader> loader,
            const webrtc::SdpVideoFormat& format = webrtc::SdpVideoFormat("H264"));

    explicit NvencVideoEncoder(
            std::shared_ptr<NvencLoader> loader,
            const webrtc::SdpVideoFormat& format = webrtc::SdpVideoFormat("H264"));
    ~NvencVideoEncoder() override;

    int32_t InitEncode(const webrtc::VideoCodec* codec_settings,
                       const webrtc::VideoEncoder::Settings& settings) override;
    int32_t RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback) override;
    int32_t Release() override;
    int32_t Encode(const webrtc::VideoFrame& frame,
                   const std::vector<webrtc::VideoFrameType>* frame_types) override;
    void SetRates(const RateControlParameters& parameters) override;
    EncoderInfo GetEncoderInfo() const override;

  private:
    absl::Status InitializeSessionLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
    void DestroySessionLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

    const std::shared_ptr<NvencLoader> loader_;
    const webrtc::SdpVideoFormat format_;
    webrtc::CodecSpecificInfo codec_specific_info_;

    mutable absl::Mutex mutex_;
    bool initialized_ ABSL_GUARDED_BY(mutex_){false};
    void* encoder_session_ ABSL_GUARDED_BY(mutex_){nullptr};

    int32_t width_ ABSL_GUARDED_BY(mutex_){0};
    int32_t height_ ABSL_GUARDED_BY(mutex_){0};
    uint32_t target_bitrate_bps_ ABSL_GUARDED_BY(mutex_){0};
    uint32_t max_framerate_fps_ ABSL_GUARDED_BY(mutex_){30};

    NV_ENC_INPUT_PTR input_buffer_ ABSL_GUARDED_BY(mutex_){nullptr};
    NV_ENC_OUTPUT_PTR bitstream_buffer_ ABSL_GUARDED_BY(mutex_){nullptr};
    GUID profile_guid_ ABSL_GUARDED_BY(mutex_){NV_ENC_H264_PROFILE_BASELINE_GUID};
    NV_ENC_CONFIG encode_config_ ABSL_GUARDED_BY(mutex_){};

    webrtc::EncodedImageCallback* callback_ ABSL_GUARDED_BY(mutex_){nullptr};
};

}  // namespace goldfish::videobridge
