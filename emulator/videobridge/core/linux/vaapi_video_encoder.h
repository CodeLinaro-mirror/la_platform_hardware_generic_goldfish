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
#include <memory>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"

#include "api/video/video_frame.h"
#include "api/video_codecs/h264_profile_level_id.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "modules/video_coding/include/video_codec_interface.h"
#pragma clang diagnostic pop

#include "core/linux/va_api_types.h"
#include "core/linux/vaapi_loader.h"

namespace goldfish::videobridge {

/**
 * @brief Maps WebRTC H264Profile enum to the corresponding VA-API profile constant.
 */
inline VAProfile H264ProfileToVaapiProfile(webrtc::H264Profile profile) {
    switch (profile) {
    case webrtc::H264Profile::kProfileMain:
        return VAProfileH264Main;
    case webrtc::H264Profile::kProfileHigh:
    case webrtc::H264Profile::kProfileConstrainedHigh:
        return VAProfileH264High;
    case webrtc::H264Profile::kProfileConstrainedBaseline:
    case webrtc::H264Profile::kProfileBaseline:
    default:
        return VAProfileH264ConstrainedBaseline;
    }
}

/**
 * @class VaapiVideoEncoder
 * @brief WebRTC VideoEncoder implementation utilizing Intel/AMD VA-API hardware encoding.
 */
class VaapiVideoEncoder : public webrtc::VideoEncoder {
  public:
    static std::unique_ptr<VaapiVideoEncoder> Create(
            std::shared_ptr<VaapiLoader> loader,
            const webrtc::SdpVideoFormat& format = webrtc::SdpVideoFormat("H264"));

    static std::unique_ptr<VaapiVideoEncoder> Create(std::shared_ptr<VaapiLoader> loader,
                                                     VAProfile profile);

    explicit VaapiVideoEncoder(
            std::shared_ptr<VaapiLoader> loader,
            const webrtc::SdpVideoFormat& format = webrtc::SdpVideoFormat("H264"));

    VaapiVideoEncoder(std::shared_ptr<VaapiLoader> loader, VAProfile profile);
    ~VaapiVideoEncoder() override;

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

    const std::shared_ptr<VaapiLoader> loader_;
    const webrtc::SdpVideoFormat format_;
    VAProfile profile_{VAProfileH264ConstrainedBaseline};
    webrtc::CodecSpecificInfo codec_specific_info_;

    mutable absl::Mutex mutex_;
    bool initialized_ ABSL_GUARDED_BY(mutex_){false};

    VAConfigID config_id_ ABSL_GUARDED_BY(mutex_){VA_INVALID_ID};
    VAContextID context_id_ ABSL_GUARDED_BY(mutex_){VA_INVALID_ID};
    VASurfaceID surface_id_ ABSL_GUARDED_BY(mutex_){VA_INVALID_SURFACE};
    VABufferID coded_buf_id_ ABSL_GUARDED_BY(mutex_){VA_INVALID_ID};

    uint32_t width_ ABSL_GUARDED_BY(mutex_){0};
    uint32_t height_ ABSL_GUARDED_BY(mutex_){0};
    uint32_t target_bitrate_bps_ ABSL_GUARDED_BY(mutex_){2000000};
    uint32_t max_framerate_fps_ ABSL_GUARDED_BY(mutex_){30};
    uint32_t frame_num_ ABSL_GUARDED_BY(mutex_){0};

    webrtc::EncodedImageCallback* callback_ ABSL_GUARDED_BY(mutex_){nullptr};
};

}  // namespace goldfish::videobridge
