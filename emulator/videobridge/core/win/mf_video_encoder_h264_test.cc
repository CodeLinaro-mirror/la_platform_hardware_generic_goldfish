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
#include "core/win/mf_video_encoder_h264.h"

#include <gtest/gtest.h>

#include "api/video_codecs/video_encoder.h"
#include "modules/video_coding/include/video_error_codes.h"

namespace goldfish::videobridge {

class MFVideoEncoderH264Test : public ::testing::Test {
  protected:
    void SetUp() override { encoder_ = std::make_unique<MFVideoEncoderH264>(); }

    std::unique_ptr<MFVideoEncoderH264> encoder_;
};

TEST_F(MFVideoEncoderH264Test, InitEncodeFailsWithInvalidCodec) {
    webrtc::VideoCodec settings;
    settings.codecType = webrtc::kVideoCodecVP8;  // Only H264 is supported
    settings.width = 640;
    settings.height = 480;
    settings.startBitrate = 3000;
    settings.maxFramerate = 30;

    int32_t result = encoder_->InitEncode(&settings, 1, 1200);
    EXPECT_EQ(result, WEBRTC_VIDEO_CODEC_ERR_PARAMETER);
}

TEST_F(MFVideoEncoderH264Test, InitEncodeFailsWithNullSettings) {
    int32_t result = encoder_->InitEncode(nullptr, 1, 1200);
    EXPECT_EQ(result, WEBRTC_VIDEO_CODEC_ERR_PARAMETER);
}

TEST_F(MFVideoEncoderH264Test, GetEncoderInfoReturnsCorrectMetadata) {
    webrtc::VideoEncoder::EncoderInfo info = encoder_->GetEncoderInfo();
    EXPECT_FALSE(info.supports_native_handle);
    EXPECT_EQ(info.implementation_name, "WindowsMediaFoundationH264");
    EXPECT_TRUE(info.is_hardware_accelerated);
}

}  // namespace goldfish::videobridge
