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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>

#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/nv12_buffer.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/video_encoder.h"
#include "core/linux/nvenc_video_encoder_factory.h"
#include "core/linux/test/fake_nvenc_driver.h"
#include "modules/video_coding/include/video_error_codes.h"

namespace goldfish::videobridge {
namespace {

using ::testing::_;

class MockEncodedImageCallback : public webrtc::EncodedImageCallback {
  public:
    MOCK_METHOD(Result, OnEncodedImage,
                (const webrtc::EncodedImage& encoded_image,
                 const webrtc::CodecSpecificInfo* codec_specific_info),
                (override));
    void OnFrameDropped(uint32_t /*rtp_timestamp*/, int /*spatial_id*/,
                        bool /*is_end_of_temporal_unit*/) override {}
};

webrtc::VideoFrame CreateTestI420Frame(int64_t timestamp_us) {
    webrtc::scoped_refptr<webrtc::I420Buffer> buffer = webrtc::I420Buffer::Create(1280, 720);
    std::memset(buffer->MutableDataY(), 128, static_cast<size_t>(buffer->StrideY()) * 720);
    std::memset(buffer->MutableDataU(), 64, static_cast<size_t>(buffer->StrideU()) * 360);
    std::memset(buffer->MutableDataV(), 192, static_cast<size_t>(buffer->StrideV()) * 360);

    return webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(buffer)
            .set_timestamp_us(timestamp_us)
            .set_rotation(webrtc::kVideoRotation_0)
            .build();
}

webrtc::VideoFrame CreateTestNv12Frame(int64_t timestamp_us) {
    webrtc::scoped_refptr<webrtc::NV12Buffer> buffer = webrtc::NV12Buffer::Create(1280, 720);
    std::memset(buffer->MutableDataY(), 128, static_cast<size_t>(buffer->StrideY()) * 720);
    std::memset(buffer->MutableDataUV(), 64, static_cast<size_t>(buffer->StrideUV()) * 360);

    return webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(buffer)
            .set_timestamp_us(timestamp_us)
            .set_rotation(webrtc::kVideoRotation_0)
            .build();
}

class NvencVideoEncoderTest : public ::testing::Test {
  protected:
    void SetUp() override {
        driver_ = std::make_unique<FakeNvencDriver>();
        auto loader_status = NvencLoader::CreateForTest(driver_->CreateFunctionList());
        ASSERT_TRUE(loader_status.ok());
        loader_ = std::move(*loader_status);

        encoder_ = NvencVideoEncoder::Create(loader_);
        ASSERT_NE(encoder_, nullptr);

        codec_settings_.width = 1280;
        codec_settings_.height = 720;
        codec_settings_.maxFramerate = 30;
        codec_settings_.startBitrate = 2000;  // kbps
        codec_settings_.maxBitrate = 4000;
        codec_settings_.codecType = webrtc::kVideoCodecH264;
    }

    std::unique_ptr<FakeNvencDriver> driver_;
    std::shared_ptr<NvencLoader> loader_;
    std::unique_ptr<NvencVideoEncoder> encoder_;
    webrtc::VideoCodec codec_settings_{};
    MockEncodedImageCallback callback_;
};

TEST_F(NvencVideoEncoderTest, InitEncodeSucceeds) {
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    int32_t ret = encoder_->InitEncode(&codec_settings_, settings);
    EXPECT_EQ(ret, WEBRTC_VIDEO_CODEC_OK);
    EXPECT_TRUE(driver_->session_active());
}

TEST_F(NvencVideoEncoderTest, ConfiguresNegotiatedH264Profile) {
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);

    // Constrained Baseline Profile ("42e01f")
    webrtc::SdpVideoFormat cbp_format("H264", {{"profile-level-id", "42e01f"}});
    auto cbp_encoder = NvencVideoEncoder::Create(loader_, cbp_format);
    ASSERT_NE(cbp_encoder, nullptr);
    ASSERT_EQ(cbp_encoder->InitEncode(&codec_settings_, settings), WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(std::memcmp(&driver_->last_profile_guid(), &NV_ENC_H264_PROFILE_BASELINE_GUID,
                          sizeof(GUID)),
              0);

    // Main Profile ("4d001f")
    webrtc::SdpVideoFormat mp_format("H264", {{"profile-level-id", "4d001f"}});
    auto mp_encoder = NvencVideoEncoder::Create(loader_, mp_format);
    ASSERT_NE(mp_encoder, nullptr);
    ASSERT_EQ(mp_encoder->InitEncode(&codec_settings_, settings), WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(std::memcmp(&driver_->last_profile_guid(), &NV_ENC_H264_PROFILE_MAIN_GUID,
                          sizeof(GUID)),
              0);

    // High Profile ("640c1f")
    webrtc::SdpVideoFormat hp_format("H264", {{"profile-level-id", "640c1f"}});
    auto hp_encoder = NvencVideoEncoder::Create(loader_, hp_format);
    ASSERT_NE(hp_encoder, nullptr);
    ASSERT_EQ(hp_encoder->InitEncode(&codec_settings_, settings), WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(std::memcmp(&driver_->last_profile_guid(), &NV_ENC_H264_PROFILE_HIGH_GUID,
                          sizeof(GUID)),
              0);
}

TEST_F(NvencVideoEncoderTest, EncodesI420FrameAndDeliversImage) {
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    ASSERT_EQ(encoder_->InitEncode(&codec_settings_, settings), WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(encoder_->RegisterEncodeCompleteCallback(&callback_), WEBRTC_VIDEO_CODEC_OK);

    webrtc::EncodedImage captured_image;
    EXPECT_CALL(callback_, OnEncodedImage(_, _))
            .WillOnce([&captured_image](const webrtc::EncodedImage& image,
                                        const webrtc::CodecSpecificInfo*) {
                captured_image = image;
                return webrtc::EncodedImageCallback::Result(
                        webrtc::EncodedImageCallback::Result::OK);
            });

    std::vector<webrtc::VideoFrameType> frame_types = {webrtc::VideoFrameType::kVideoFrameKey};
    webrtc::VideoFrame frame = CreateTestI420Frame(1000);

    int32_t ret = encoder_->Encode(frame, &frame_types);
    EXPECT_EQ(ret, WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(driver_->encode_calls(), 1);
    EXPECT_EQ(driver_->force_idr_calls(), 1);
    EXPECT_EQ(captured_image._frameType, webrtc::VideoFrameType::kVideoFrameKey);
    EXPECT_GT(captured_image.size(), 0U);

    // Encode a second frame as delta frame
    std::vector<webrtc::VideoFrameType> delta_types = {webrtc::VideoFrameType::kVideoFrameDelta};
    webrtc::VideoFrame delta_frame = CreateTestI420Frame(2000);
    EXPECT_CALL(callback_, OnEncodedImage(_, _))
            .WillOnce([&captured_image](const webrtc::EncodedImage& image,
                                        const webrtc::CodecSpecificInfo*) {
                captured_image = image;
                return webrtc::EncodedImageCallback::Result(
                        webrtc::EncodedImageCallback::Result::OK);
            });

    ret = encoder_->Encode(delta_frame, &delta_types);
    EXPECT_EQ(ret, WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(driver_->encode_calls(), 2);
    EXPECT_EQ(driver_->force_idr_calls(), 1);
    EXPECT_EQ(captured_image._frameType, webrtc::VideoFrameType::kVideoFrameDelta);
}

TEST_F(NvencVideoEncoderTest, EncodesNv12DirectBufferSuccessfully) {
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    ASSERT_EQ(encoder_->InitEncode(&codec_settings_, settings), WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(encoder_->RegisterEncodeCompleteCallback(&callback_), WEBRTC_VIDEO_CODEC_OK);

    webrtc::EncodedImage captured_image;
    EXPECT_CALL(callback_, OnEncodedImage(_, _))
            .WillOnce([&captured_image](const webrtc::EncodedImage& image,
                                        const webrtc::CodecSpecificInfo*) {
                captured_image = image;
                return webrtc::EncodedImageCallback::Result(
                        webrtc::EncodedImageCallback::Result::OK);
            });

    std::vector<webrtc::VideoFrameType> frame_types = {webrtc::VideoFrameType::kVideoFrameKey};
    webrtc::VideoFrame nv12_frame = CreateTestNv12Frame(1500);

    int32_t ret = encoder_->Encode(nv12_frame, &frame_types);
    EXPECT_EQ(ret, WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(driver_->encode_calls(), 1);
    EXPECT_EQ(driver_->force_idr_calls(), 1);
    EXPECT_EQ(captured_image._frameType, webrtc::VideoFrameType::kVideoFrameKey);
}

TEST_F(NvencVideoEncoderTest, EncodeFailsOnDimensionMismatch) {
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    ASSERT_EQ(encoder_->InitEncode(&codec_settings_, settings), WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(encoder_->RegisterEncodeCompleteCallback(&callback_), WEBRTC_VIDEO_CODEC_OK);

    // Frame with 640x480 instead of configured 1280x720
    webrtc::scoped_refptr<webrtc::I420Buffer> mismatched_buffer =
            webrtc::I420Buffer::Create(640, 480);
    webrtc::VideoFrame mismatched_frame = webrtc::VideoFrame::Builder()
                                                  .set_video_frame_buffer(mismatched_buffer)
                                                  .set_timestamp_us(1000)
                                                  .build();

    std::vector<webrtc::VideoFrameType> frame_types = {webrtc::VideoFrameType::kVideoFrameKey};
    int32_t ret = encoder_->Encode(mismatched_frame, &frame_types);
    EXPECT_EQ(ret, WEBRTC_VIDEO_CODEC_ERR_PARAMETER);
}

TEST_F(NvencVideoEncoderTest, InitEncodeFailsWithNullSettings) {
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    EXPECT_EQ(encoder_->InitEncode(nullptr, settings), WEBRTC_VIDEO_CODEC_ERR_PARAMETER);
}

TEST_F(NvencVideoEncoderTest, EncodeFailsWhenUninitialized) {
    webrtc::VideoFrame frame = CreateTestI420Frame(1000);
    std::vector<webrtc::VideoFrameType> frame_types = {webrtc::VideoFrameType::kVideoFrameKey};
    EXPECT_EQ(encoder_->Encode(frame, &frame_types), WEBRTC_VIDEO_CODEC_UNINITIALIZED);
}

TEST_F(NvencVideoEncoderTest, SetRatesReconfiguresBitrate) {
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    ASSERT_EQ(encoder_->InitEncode(&codec_settings_, settings), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoEncoder::RateControlParameters rates;
    rates.bitrate.SetBitrate(0, 0, 3000000);  // 3 Mbps
    rates.framerate_fps = 60.0;

    encoder_->SetRates(rates);
    EXPECT_EQ(driver_->reconfigure_calls(), 1);
    EXPECT_EQ(driver_->last_bitrate(), 3000000U);
    EXPECT_EQ(driver_->last_framerate(), 60U);
}

TEST_F(NvencVideoEncoderTest, ReleaseDestroysSession) {
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    ASSERT_EQ(encoder_->InitEncode(&codec_settings_, settings), WEBRTC_VIDEO_CODEC_OK);
    EXPECT_TRUE(driver_->session_active());

    EXPECT_EQ(encoder_->Release(), WEBRTC_VIDEO_CODEC_OK);
    EXPECT_FALSE(driver_->session_active());
}

TEST(NvencVideoEncoderFactoryTest, AdvertisesProbedH264Formats) {
    FakeNvencDriver driver;
    auto loader = *NvencLoader::CreateForTest(driver.CreateFunctionList());
    auto factory = NvencVideoEncoderFactory::CreateForTest(std::move(loader));
    ASSERT_NE(factory, nullptr);

    auto formats = factory->GetSupportedFormats();
    EXPECT_EQ(formats.size(), 16U);

    bool has_cbp_31 = false;
    bool has_main_31 = false;
    bool has_high_31 = false;
    for (const auto& fmt : formats) {
        EXPECT_EQ(fmt.name, "H264");
        auto it = fmt.parameters.find("profile-level-id");
        if (it != fmt.parameters.end()) {
            if (it->second == "42e01f") has_cbp_31 = true;
            if (it->second == "4d001f") has_main_31 = true;
            if (it->second == "640c1f") has_high_31 = true;
        }
    }
    EXPECT_TRUE(has_cbp_31);
    EXPECT_TRUE(has_main_31);
    EXPECT_TRUE(has_high_31);
}

TEST(NvencVideoEncoderFactoryTest, CreatesEncoderForH264) {
    FakeNvencDriver driver;
    auto loader = *NvencLoader::CreateForTest(driver.CreateFunctionList());
    auto factory = NvencVideoEncoderFactory::CreateForTest(std::move(loader));
    ASSERT_NE(factory, nullptr);

    webrtc::SdpVideoFormat h264_format("H264");
    auto encoder = factory->CreateVideoEncoder(h264_format);
    EXPECT_NE(encoder, nullptr);

    webrtc::SdpVideoFormat vp8_format("VP8");
    EXPECT_EQ(factory->CreateVideoEncoder(vp8_format), nullptr);
}

TEST(NvencVideoEncoderFactoryTest, PropagatesNegotiatedProfileToEncoder) {
    FakeNvencDriver driver;
    auto loader = *NvencLoader::CreateForTest(driver.CreateFunctionList());
    auto factory = NvencVideoEncoderFactory::CreateForTest(std::move(loader));
    ASSERT_NE(factory, nullptr);

    webrtc::VideoCodec codec_settings{};
    codec_settings.width = 1280;
    codec_settings.height = 720;
    codec_settings.codecType = webrtc::kVideoCodecH264;
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);

    // Constrained Baseline Profile ("42e01f")
    auto cbp_encoder = factory->CreateVideoEncoder(
            webrtc::SdpVideoFormat("H264", {{"profile-level-id", "42e01f"}}));
    ASSERT_NE(cbp_encoder, nullptr);
    ASSERT_EQ(cbp_encoder->InitEncode(&codec_settings, settings), WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(std::memcmp(&driver.last_profile_guid(), &NV_ENC_H264_PROFILE_BASELINE_GUID,
                          sizeof(GUID)),
              0);

    // Main Profile ("4d001f")
    auto mp_encoder = factory->CreateVideoEncoder(
            webrtc::SdpVideoFormat("H264", {{"profile-level-id", "4d001f"}}));
    ASSERT_NE(mp_encoder, nullptr);
    ASSERT_EQ(mp_encoder->InitEncode(&codec_settings, settings), WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(
            std::memcmp(&driver.last_profile_guid(), &NV_ENC_H264_PROFILE_MAIN_GUID, sizeof(GUID)),
            0);

    // High Profile ("640c1f")
    auto hp_encoder = factory->CreateVideoEncoder(
            webrtc::SdpVideoFormat("H264", {{"profile-level-id", "640c1f"}}));
    ASSERT_NE(hp_encoder, nullptr);
    ASSERT_EQ(hp_encoder->InitEncode(&codec_settings, settings), WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(
            std::memcmp(&driver.last_profile_guid(), &NV_ENC_H264_PROFILE_HIGH_GUID, sizeof(GUID)),
            0);
}

}  // namespace
}  // namespace goldfish::videobridge
