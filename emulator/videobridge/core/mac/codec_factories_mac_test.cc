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
#include <gtest/gtest.h>

#include "goldfish/videobridge/codec_factories.h"

// Disable compiler warnings for external third-party headers. We wrap these in localized
// pragma blocks rather than using target 'copts' so that thread-safety analysis remains
// active on our own local source files.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/environment/environment_factory.h"
#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "modules/video_coding/include/video_error_codes.h"
#pragma clang diagnostic pop

namespace goldfish::videobridge {

class MacCodecFactoriesTest : public ::testing::Test {
  protected:
    webrtc::Environment env_ = webrtc::CreateEnvironment();
};

TEST_F(MacCodecFactoriesTest, CreatePlatformVideoEncoderFactorySucceeds) {
    auto factory = CreatePlatformVideoEncoderFactory();
    ASSERT_NE(factory, nullptr);

    // Verify that the factory advertises H264 among its supported formats
    auto formats = factory->GetSupportedFormats();
    bool has_h264 = false;
    for (const auto& format : formats) {
        if (format.name == "H264") {
            has_h264 = true;
            auto it = format.parameters.find("profile-level-id");
            if (it != format.parameters.end()) {
                std::string profile_level = it->second;
                if (profile_level.size() == 6) {
                    EXPECT_EQ(profile_level.substr(4, 2), "34");
                }
            }
        }
    }
    EXPECT_TRUE(has_h264);
}

TEST_F(MacCodecFactoriesTest, CreatePlatformVideoDecoderFactorySucceeds) {
    auto factory = CreatePlatformVideoDecoderFactory();
    ASSERT_NE(factory, nullptr);

    auto formats = factory->GetSupportedFormats();
    bool has_h264 = false;
    for (const auto& format : formats) {
        if (format.name == "H264") {
            has_h264 = true;
            auto it = format.parameters.find("profile-level-id");
            if (it != format.parameters.end()) {
                std::string profile_level = it->second;
                if (profile_level.size() == 6) {
                    EXPECT_EQ(profile_level.substr(4, 2), "34");
                }
            }
        }
    }
    EXPECT_TRUE(has_h264);
}

TEST_F(MacCodecFactoriesTest, InstantiatesAndInitializesH264Encoder) {
    auto factory = CreatePlatformVideoEncoderFactory();
    ASSERT_NE(factory, nullptr);

    // Find a supported H.264 format
    webrtc::SdpVideoFormat h264_format("H264");
    bool found_h264 = false;
    for (const auto& format : factory->GetSupportedFormats()) {
        if (format.name == "H264") {
            h264_format = format;
            found_h264 = true;
            break;
        }
    }

    if (!found_h264) {
        GTEST_SKIP() << "H.264 encoder is not supported by the factory on this host.";
    }

    auto encoder = factory->Create(env_, h264_format);
    if (!encoder) {
        GTEST_SKIP() << "H.264 hardware encoder failed to instantiate on this host.";
    }

    // Initialize the encoder
    webrtc::VideoCodec codec_settings;
    codec_settings.codecType = webrtc::kVideoCodecH264;
    codec_settings.width = 640;
    codec_settings.height = 480;
    codec_settings.startBitrate = 3000;
    codec_settings.maxFramerate = 30;

    int32_t init_result = encoder->InitEncode(&codec_settings, 1, 1200);
    EXPECT_EQ(init_result, WEBRTC_VIDEO_CODEC_OK);

    // Clean up
    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

TEST_F(MacCodecFactoriesTest, CanEncodeDummyFrame) {
    auto factory = CreatePlatformVideoEncoderFactory();
    ASSERT_NE(factory, nullptr);

    // Find a supported H.264 format
    webrtc::SdpVideoFormat h264_format("H264");
    bool found_h264 = false;
    for (const auto& format : factory->GetSupportedFormats()) {
        if (format.name == "H264") {
            h264_format = format;
            found_h264 = true;
            break;
        }
    }

    if (!found_h264) {
        GTEST_SKIP() << "H.264 encoder is not supported by the factory on this host.";
    }

    auto encoder = factory->Create(env_, h264_format);
    if (!encoder) {
        GTEST_SKIP() << "H.264 hardware encoder failed to instantiate on this host.";
    }

    webrtc::VideoCodec codec_settings;
    codec_settings.codecType = webrtc::kVideoCodecH264;
    codec_settings.width = 320;
    codec_settings.height = 240;
    codec_settings.startBitrate = 1000;
    codec_settings.maxFramerate = 30;

    ASSERT_EQ(encoder->InitEncode(&codec_settings, 1, 1200), WEBRTC_VIDEO_CODEC_OK);

    // Create a dummy I420 frame
    webrtc::scoped_refptr<webrtc::I420Buffer> buffer = webrtc::I420Buffer::Create(320, 240);
    // Fill the buffer with dummy YUV data
    memset(buffer->MutableDataY(), 128, buffer->StrideY() * 240);
    memset(buffer->MutableDataU(), 128, buffer->StrideU() * 120);
    memset(buffer->MutableDataV(), 128, buffer->StrideV() * 120);

    webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                       .set_video_frame_buffer(buffer)
                                       .set_rtp_timestamp(0)
                                       .set_timestamp_us(0)
                                       .build();

    std::vector<webrtc::VideoFrameType> frame_types = {webrtc::VideoFrameType::kVideoFrameKey};

    int32_t encode_result = encoder->Encode(frame, &frame_types);
    EXPECT_TRUE(encode_result == WEBRTC_VIDEO_CODEC_OK ||
                encode_result == WEBRTC_VIDEO_CODEC_UNINITIALIZED);

    encoder->Release();
}

}  // namespace goldfish::videobridge
