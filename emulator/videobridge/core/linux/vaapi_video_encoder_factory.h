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
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#pragma clang diagnostic pop

#include "core/linux/vaapi_loader.h"

namespace goldfish::videobridge {

/**
 * @class VaapiVideoEncoderFactory
 * @brief WebRTC VideoEncoderFactory that advertises and instantiates Intel/AMD VA-API hardware
 * encoders.
 */
class VaapiVideoEncoderFactory : public webrtc::VideoEncoderFactory {
  public:
    static std::unique_ptr<VaapiVideoEncoderFactory> Create();
    static std::unique_ptr<VaapiVideoEncoderFactory> CreateForTest(
            std::shared_ptr<VaapiLoader> loader);

    explicit VaapiVideoEncoderFactory(std::shared_ptr<VaapiLoader> loader);
    ~VaapiVideoEncoderFactory() override = default;

    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

    std::unique_ptr<webrtc::VideoEncoder> Create(const webrtc::Environment& env,
                                                 const webrtc::SdpVideoFormat& format) override;

    std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat& format);

  private:
    void ProbeHardwareCapabilities();

    const std::shared_ptr<VaapiLoader> loader_;
    std::vector<webrtc::SdpVideoFormat> supported_formats_;
};

}  // namespace goldfish::videobridge
