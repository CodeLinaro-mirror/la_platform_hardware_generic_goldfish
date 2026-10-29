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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "api/scoped_refptr.h"
#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/nv12_buffer.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/video_encoder.h"
#include "core/linux/test/fake_vaapi_driver.h"
#include "core/linux/test/vaapi_bitstream_test_utils.h"
#include "core/linux/vaapi_video_encoder_factory.h"
#include "modules/video_coding/include/video_error_codes.h"

VAAPI_DEFINE_HEAP_TRACKER_NEW_DELETE()

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

class VaapiVideoEncoderTest : public ::testing::Test {
  protected:
    void SetUp() override {
        driver_ = std::make_unique<FakeVaapiDriver>();
        auto loader_status =
                VaapiLoader::CreateForTest(driver_->CreateFunctionList(), driver_->display());
        ASSERT_TRUE(loader_status.ok());
        loader_ = std::move(*loader_status);

        encoder_ = VaapiVideoEncoder::Create(loader_, VAProfileH264ConstrainedBaseline);
        ASSERT_NE(encoder_, nullptr);

        codec_settings_.width = 1280;
        codec_settings_.height = 720;
        codec_settings_.maxFramerate = 30;
        codec_settings_.startBitrate = 2000;  // kbps
        codec_settings_.maxBitrate = 4000;
        codec_settings_.codecType = webrtc::kVideoCodecH264;
    }

    std::unique_ptr<FakeVaapiDriver> driver_;
    std::shared_ptr<VaapiLoader> loader_;
    std::unique_ptr<VaapiVideoEncoder> encoder_;
    webrtc::VideoCodec codec_settings_{};
    MockEncodedImageCallback callback_;
};

TEST_F(VaapiVideoEncoderTest, InitEncodeSucceeds) {
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    int32_t ret = encoder_->InitEncode(&codec_settings_, settings);
    EXPECT_EQ(ret, WEBRTC_VIDEO_CODEC_OK);
}

TEST_F(VaapiVideoEncoderTest, EncodesI420FrameAndDeliversImage) {
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

TEST_F(VaapiVideoEncoderTest, EncodesNv12DirectBufferSuccessfully) {
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

TEST_F(VaapiVideoEncoderTest, EncodeFailsOnDimensionMismatch) {
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

TEST_F(VaapiVideoEncoderTest, InitEncodeFailsWithNullSettings) {
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    EXPECT_EQ(encoder_->InitEncode(nullptr, settings), WEBRTC_VIDEO_CODEC_ERR_PARAMETER);
}

TEST_F(VaapiVideoEncoderTest, EncodeFailsWhenUninitialized) {
    webrtc::VideoFrame frame = CreateTestI420Frame(1000);
    std::vector<webrtc::VideoFrameType> frame_types = {webrtc::VideoFrameType::kVideoFrameKey};
    EXPECT_EQ(encoder_->Encode(frame, &frame_types), WEBRTC_VIDEO_CODEC_UNINITIALIZED);
}

TEST_F(VaapiVideoEncoderTest, SetRatesReconfiguresBitrate) {
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    ASSERT_EQ(encoder_->InitEncode(&codec_settings_, settings), WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(encoder_->RegisterEncodeCompleteCallback(&callback_), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoEncoder::RateControlParameters rates;
    rates.bitrate.SetBitrate(0, 0, 3000000);  // 3 Mbps
    rates.framerate_fps = 60.0;

    encoder_->SetRates(rates);

    webrtc::VideoFrame frame = CreateTestI420Frame(1000);
    std::vector<webrtc::VideoFrameType> frame_types = {webrtc::VideoFrameType::kVideoFrameKey};
    EXPECT_CALL(callback_, OnEncodedImage(_, _))
            .WillOnce(::testing::Return(webrtc::EncodedImageCallback::Result(
                    webrtc::EncodedImageCallback::Result::OK)));

    int32_t ret = encoder_->Encode(frame, &frame_types);
    EXPECT_EQ(ret, WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(driver_->reconfigure_calls(), 1);
    EXPECT_EQ(driver_->last_bitrate(), 3000000U);
}

TEST_F(VaapiVideoEncoderTest, ReleaseDestroysSession) {
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    ASSERT_EQ(encoder_->InitEncode(&codec_settings_, settings), WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(encoder_->Release(), WEBRTC_VIDEO_CODEC_OK);
}

TEST(VaapiVideoEncoderFactoryTest, AdvertisesProbedH264Formats) {
    FakeVaapiDriver driver;
    auto loader_status = VaapiLoader::CreateForTest(driver.CreateFunctionList(), driver.display());
    ASSERT_TRUE(loader_status.ok());
    auto factory = VaapiVideoEncoderFactory::CreateForTest(std::move(*loader_status));
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

TEST(VaapiVideoEncoderFactoryTest, CreatesEncoderForH264) {
    FakeVaapiDriver driver;
    auto loader_status = VaapiLoader::CreateForTest(driver.CreateFunctionList(), driver.display());
    ASSERT_TRUE(loader_status.ok());
    auto factory = VaapiVideoEncoderFactory::CreateForTest(std::move(*loader_status));
    ASSERT_NE(factory, nullptr);

    webrtc::SdpVideoFormat h264_format("H264");
    auto encoder = factory->CreateVideoEncoder(h264_format);
    EXPECT_NE(encoder, nullptr);

    webrtc::SdpVideoFormat vp8_format("VP8");
    EXPECT_EQ(factory->CreateVideoEncoder(vp8_format), nullptr);
}

TEST(VaapiVideoEncoderFactoryTest, PropagatesNegotiatedProfileToEncoder) {
    FakeVaapiDriver driver;
    auto loader_status = VaapiLoader::CreateForTest(driver.CreateFunctionList(), driver.display());
    ASSERT_TRUE(loader_status.ok());
    auto factory = VaapiVideoEncoderFactory::CreateForTest(std::move(*loader_status));
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
    EXPECT_EQ(driver.last_profile(), VAProfileH264ConstrainedBaseline);

    // Main Profile ("4d001f")
    auto mp_encoder = factory->CreateVideoEncoder(
            webrtc::SdpVideoFormat("H264", {{"profile-level-id", "4d001f"}}));
    ASSERT_NE(mp_encoder, nullptr);
    ASSERT_EQ(mp_encoder->InitEncode(&codec_settings, settings), WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(driver.last_profile(), VAProfileH264Main);

    // High Profile ("640c1f")
    auto hp_encoder = factory->CreateVideoEncoder(
            webrtc::SdpVideoFormat("H264", {{"profile-level-id", "640c1f"}}));
    ASSERT_NE(hp_encoder, nullptr);
    ASSERT_EQ(hp_encoder->InitEncode(&codec_settings, settings), WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(driver.last_profile(), VAProfileH264High);
}

TEST_F(VaapiVideoEncoderTest, EncodesMultiSegmentDeltaBitstreamWithoutLoss) {
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    ASSERT_EQ(encoder_->InitEncode(&codec_settings_, settings), WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(encoder_->RegisterEncodeCompleteCallback(&callback_), WEBRTC_VIDEO_CODEC_OK);

    EXPECT_CALL(callback_, OnEncodedImage(_, _))
            .WillOnce(::testing::Return(webrtc::EncodedImageCallback::Result(
                    webrtc::EncodedImageCallback::Result::OK)));
    const std::vector<webrtc::VideoFrameType> key_types = {webrtc::VideoFrameType::kVideoFrameKey};
    ASSERT_EQ(encoder_->Encode(CreateTestI420Frame(1000), &key_types), WEBRTC_VIDEO_CODEC_OK);

    driver_->SetCodedSegments({
        {0x00U, 0x00U, 0x00U, 0x01U, 0x41U},
        {},
        {0x9AU, 0x00U, 0x10U},
        {0x20U, 0x30U, 0x40U},
    });

    webrtc::EncodedImage captured_delta;
    EXPECT_CALL(callback_, OnEncodedImage(_, _))
            .WillOnce([&captured_delta](const webrtc::EncodedImage& image,
                                        const webrtc::CodecSpecificInfo*) {
                captured_delta = image;
                return webrtc::EncodedImageCallback::Result(
                        webrtc::EncodedImageCallback::Result::OK);
            });

    const std::vector<webrtc::VideoFrameType> delta_types = {
        webrtc::VideoFrameType::kVideoFrameDelta};
    ASSERT_EQ(encoder_->Encode(CreateTestI420Frame(2000), &delta_types), WEBRTC_VIDEO_CODEC_OK);

    const std::vector<uint8_t> expected_delta = {
        0x00U, 0x00U, 0x00U, 0x01U, 0x41U, 0x9AU, 0x00U, 0x10U, 0x20U, 0x30U, 0x40U,
    };
    ASSERT_EQ(captured_delta.size(), expected_delta.size());
    EXPECT_EQ(std::memcmp(captured_delta.data(), expected_delta.data(), expected_delta.size()), 0);
    EXPECT_GT(VaapiVideoEncoderTestPeer::ScratchBitstreamCapacity(*encoder_), 0U);

    ASSERT_EQ(encoder_->Release(), WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(VaapiVideoEncoderTestPeer::ScratchBitstreamCapacity(*encoder_), 0U);
}

TEST_F(VaapiVideoEncoderTest, EncodesMultiSegmentIdrWithSplitStartCodeAndReplacesMismatchedSpsPps) {
    auto high_encoder = VaapiVideoEncoder::Create(loader_, VAProfileH264High);
    ASSERT_NE(high_encoder, nullptr);

    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    ASSERT_EQ(high_encoder->InitEncode(&codec_settings_, settings), WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(high_encoder->RegisterEncodeCompleteCallback(&callback_), WEBRTC_VIDEO_CODEC_OK);

    // Baseline SPS (0x42) + PPS + IDR slice with start codes split across segment boundaries.
    driver_->SetCodedSegments({
        {0x00U, 0x00U},
        {0x00U, 0x01U, 0x67U},
        {0x42U, 0xC0U, 0x1FU},
        {0x00U, 0x00U, 0x01U, 0x68U, 0xCEU, 0x38U, 0x80U, 0x00U, 0x00U},
        {0x01U, 0x65U, 0x88U, 0x84U, 0x11U, 0x22U},
    });

    webrtc::EncodedImage captured_idr;
    EXPECT_CALL(callback_, OnEncodedImage(_, _))
            .WillOnce([&captured_idr](const webrtc::EncodedImage& image,
                                      const webrtc::CodecSpecificInfo*) {
                captured_idr = image;
                return webrtc::EncodedImageCallback::Result(
                        webrtc::EncodedImageCallback::Result::OK);
            });

    const std::vector<webrtc::VideoFrameType> key_types = {webrtc::VideoFrameType::kVideoFrameKey};
    ASSERT_EQ(high_encoder->Encode(CreateTestI420Frame(1000), &key_types), WEBRTC_VIDEO_CODEC_OK);

    const std::vector<uint8_t> encoded_vec(captured_idr.data(),
                                           captured_idr.data() + captured_idr.size());
    EXPECT_TRUE(ReferenceHasMatchingSpsNalu(encoded_vec, /*expected_profile_idc=*/100U));

    const std::vector<uint8_t> expected_tail_slice = {
        0x00U, 0x00U, 0x01U, 0x65U, 0x88U, 0x84U, 0x11U, 0x22U,
    };
    ASSERT_GT(encoded_vec.size(), expected_tail_slice.size());
    EXPECT_EQ(std::memcmp(encoded_vec.data() + (encoded_vec.size() - expected_tail_slice.size()),
                          expected_tail_slice.data(), expected_tail_slice.size()),
              0);
}

TEST(VaapiBitstreamAssemblyTest, AssembleEncodedBitstreamMatchesReferenceAcrossEdgeCases) {
    const std::vector<uint8_t> custom_sps = {0x00U, 0x00U, 0x00U, 0x01U,
                                             0x67U, 0x64U, 0x00U, 0x29U};
    const std::vector<uint8_t> custom_pps = {0x00U, 0x00U, 0x00U, 0x01U,
                                             0x68U, 0xEEU, 0x3CU, 0x80U};

    const std::vector<std::vector<uint8_t>> test_streams = {
        // Matching High-profile SPS + PPS + IDR slice.
        {0x00U, 0x00U, 0x00U, 0x01U, 0x67U, 0x64U, 0x00U, 0x1FU, 0x00U, 0x00U, 0x00U, 0x01U,
         0x68U, 0xCEU, 0x38U, 0x80U, 0x00U, 0x00U, 0x00U, 0x01U, 0x65U, 0x88U, 0x84U, 0xAAU},
        // Mismatched Baseline SPS + PPS + 3-byte IDR start code.
        {0x00U, 0x00U, 0x00U, 0x01U, 0x67U, 0x42U, 0xC0U, 0x1FU, 0x00U, 0x00U, 0x01U,
         0x68U, 0xCEU, 0x38U, 0x80U, 0x00U, 0x00U, 0x01U, 0x65U, 0x88U, 0x84U, 0x55U},
        // AUD (type 9) before mismatched SPS + PPS + IDR slice.
        {0x00U, 0x00U, 0x00U, 0x01U, 0x09U, 0xF0U, 0x00U, 0x00U, 0x00U, 0x01U, 0x67U, 0x42U, 0x00U,
         0x00U, 0x01U, 0x65U, 0x11U, 0x22U},
        // Slice-only IDR with no SPS/PPS and trailing zero before 4-byte start code.
        {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U, 0x65U, 0x99U, 0xAAU, 0xBBU},
        // SPS with zero byte as profile_idc followed immediately by 3-byte start code.
        {0x00U, 0x00U, 0x01U, 0x67U, 0x00U, 0x00U, 0x01U, 0x65U, 0x77U},
    };

    std::vector<uint8_t> scratch_bitstream;
    for (const auto& stream : test_streams) {
        // Split into 1-byte segments to exercise every possible cross-segment start-code split.
        std::vector<std::vector<uint8_t>> single_byte_chunks;
        single_byte_chunks.reserve(stream.size());
        for (uint8_t byte : stream) {
            single_byte_chunks.push_back({byte});
        }
        auto nodes = LinkCodedSegments(single_byte_chunks);

        for (bool force_idr : {false, true}) {
            auto expected = AssembleEncodedBitstreamBaseline(
                    nodes.data(), force_idr, VAProfileH264High, custom_sps, custom_pps);
            auto actual = VaapiVideoEncoderTestPeer::AssembleEncodedBitstream(
                    nodes.data(), force_idr, VAProfileH264High, custom_sps, custom_pps,
                    scratch_bitstream);
            ASSERT_EQ(actual->size(), expected->size());
            EXPECT_EQ(std::memcmp(actual->data(), expected->data(), expected->size()), 0);
        }
    }

    // Empty coded segment fallback path (size == 0, buf == nullptr).
    std::vector<uint8_t> raw_backing(64U, 0x3CU);
    auto* empty_seg = reinterpret_cast<VACodedBufferSegment*>(raw_backing.data());
    *empty_seg = VACodedBufferSegment{};
    for (bool force_idr : {false, true}) {
        auto expected = AssembleEncodedBitstreamBaseline(empty_seg, force_idr, VAProfileH264High,
                                                         custom_sps, custom_pps);
        auto actual = VaapiVideoEncoderTestPeer::AssembleEncodedBitstream(
                empty_seg, force_idr, VAProfileH264High, custom_sps, custom_pps, scratch_bitstream);
        ASSERT_EQ(actual->size(), expected->size());
        EXPECT_EQ(std::memcmp(actual->data(), expected->data(), expected->size()), 0);
    }
}

#if defined(VAAPI_TEST_ENABLE_HEAP_TRACKER)
TEST(VaapiBitstreamAssemblyTest, AssembleEncodedBitstreamEliminatesSteadyStatePayloadAllocations) {
    constexpr size_t kNumSegments = 4U;
    constexpr size_t kDeltaSegmentBytes = 16ULL * 1024ULL;
    constexpr size_t kIdrSegmentBytes = 64ULL * 1024ULL;
    constexpr size_t kMaxNaluIndexMetadataBytes = 1024U;

    const std::vector<uint8_t> custom_sps = {0x00U, 0x00U, 0x00U, 0x01U,
                                             0x67U, 0x64U, 0x00U, 0x29U};
    const std::vector<uint8_t> custom_pps = {0x00U, 0x00U, 0x00U, 0x01U,
                                             0x68U, 0xEEU, 0x3CU, 0x80U};

    // Prepare 4-segment 256 KiB IDR keyframe and 4-segment 64 KiB delta P-frame fixtures.
    std::vector<std::vector<uint8_t>> idr_chunks(kNumSegments,
                                                 std::vector<uint8_t>(kIdrSegmentBytes, 0xA5U));
    const std::vector<uint8_t> idr_prefix = {
        0x00U, 0x00U, 0x00U, 0x01U, 0x67U, 0x42U, 0xC0U, 0x1FU,  // Mismatched Baseline SPS
        0x00U, 0x00U, 0x00U, 0x01U, 0x68U, 0xCEU, 0x38U, 0x80U,  // Driver PPS
        0x00U, 0x00U, 0x00U, 0x01U, 0x65U, 0x88U, 0x84U,         // IDR slice start
    };
    std::memcpy(idr_chunks[0].data(), idr_prefix.data(), idr_prefix.size());
    auto idr_nodes = LinkCodedSegments(idr_chunks);

    std::vector<std::vector<uint8_t>> delta_chunks(kNumSegments,
                                                   std::vector<uint8_t>(kDeltaSegmentBytes, 0x5AU));
    delta_chunks[0][0] = 0x00U;
    delta_chunks[0][1] = 0x00U;
    delta_chunks[0][2] = 0x00U;
    delta_chunks[0][3] = 0x01U;
    delta_chunks[0][4] = 0x41U;
    auto delta_nodes = LinkCodedSegments(delta_chunks);

    // Warm up session scratch buffer to the initial keyframe high-water mark.
    std::vector<uint8_t> scratch_bitstream;
    auto warmup_idr = VaapiVideoEncoderTestPeer::AssembleEncodedBitstream(
            idr_nodes.data(), /*force_idr=*/true, VAProfileH264High, custom_sps, custom_pps,
            scratch_bitstream);
    ASSERT_NE(warmup_idr, nullptr);
    const size_t retained_capacity = scratch_bitstream.capacity();
    EXPECT_GE(retained_capacity, kNumSegments * kIdrSegmentBytes);

    // 1. Steady-state 4-segment 64 KiB delta P-frame.
    webrtc::scoped_refptr<webrtc::EncodedImageBuffer> baseline_delta;
    HeapAllocStats baseline_delta_stats{};
    {
        ScopedHeapTracker tracker;
        baseline_delta = AssembleEncodedBitstreamBaseline(
                delta_nodes.data(), /*force_idr=*/false, VAProfileH264High, custom_sps, custom_pps);
        baseline_delta_stats = tracker.Snapshot();
    }

    webrtc::scoped_refptr<webrtc::EncodedImageBuffer> reused_delta;
    HeapAllocStats reused_delta_stats{};
    {
        ScopedHeapTracker tracker;
        reused_delta = VaapiVideoEncoderTestPeer::AssembleEncodedBitstream(
                delta_nodes.data(), /*force_idr=*/false, VAProfileH264High, custom_sps, custom_pps,
                scratch_bitstream);
        reused_delta_stats = tracker.Snapshot();
    }

    ASSERT_EQ(reused_delta->size(), baseline_delta->size());
    EXPECT_EQ(std::memcmp(reused_delta->data(), baseline_delta->data(), reused_delta->size()), 0);
    EXPECT_EQ(scratch_bitstream.capacity(), retained_capacity);
    // Baseline allocates and frees 3 transient std::vector growth buffers (16+32+64 = 112 KiB).
    // Steady-state scratch reuse allocates only the final EncodedImageBuffer (0 transient frees).
    EXPECT_EQ(reused_delta_stats.free_count, 0U);
    EXPECT_EQ(reused_delta_stats.transient_freed_bytes, 0U);
    EXPECT_EQ(reused_delta_stats.alloc_count, 2U);
    EXPECT_GT(baseline_delta_stats.free_count, 0U);
    EXPECT_LT(reused_delta_stats.total_requested_bytes, baseline_delta_stats.total_requested_bytes);
    EXPECT_LT(reused_delta_stats.peak_live_usable_bytes,
              baseline_delta_stats.peak_live_usable_bytes);

    // 2. Steady-state 4-segment 256 KiB IDR keyframe requiring SPS/PPS header replacement.
    webrtc::scoped_refptr<webrtc::EncodedImageBuffer> baseline_idr;
    HeapAllocStats baseline_idr_stats{};
    {
        ScopedHeapTracker tracker;
        baseline_idr = AssembleEncodedBitstreamBaseline(idr_nodes.data(), /*force_idr=*/true,
                                                        VAProfileH264High, custom_sps, custom_pps);
        baseline_idr_stats = tracker.Snapshot();
    }

    webrtc::scoped_refptr<webrtc::EncodedImageBuffer> reused_idr;
    HeapAllocStats reused_idr_stats{};
    {
        ScopedHeapTracker tracker;
        reused_idr = VaapiVideoEncoderTestPeer::AssembleEncodedBitstream(
                idr_nodes.data(), /*force_idr=*/true, VAProfileH264High, custom_sps, custom_pps,
                scratch_bitstream);
        reused_idr_stats = tracker.Snapshot();
    }

    ASSERT_EQ(reused_idr->size(), baseline_idr->size());
    EXPECT_EQ(std::memcmp(reused_idr->data(), baseline_idr->data(), reused_idr->size()), 0);
    EXPECT_EQ(scratch_bitstream.capacity(), retained_capacity);
    // Steady-state IDR assembly performs zero payload buffer reallocations (only WebRTC's tiny
    // NaluIndex metadata vector is transient) and cuts per-call peak live allocations by >50%.
    EXPECT_LT(reused_idr_stats.transient_freed_bytes, kMaxNaluIndexMetadataBytes);
    EXPECT_GE(baseline_idr_stats.transient_freed_bytes, kNumSegments * kIdrSegmentBytes * 3U);
    EXPECT_LT(reused_idr_stats.total_requested_bytes,
              baseline_idr_stats.total_requested_bytes / 3U);
    EXPECT_LT(reused_idr_stats.peak_live_usable_bytes * 2U,
              baseline_idr_stats.peak_live_usable_bytes);
}
#endif  // defined(VAAPI_TEST_ENABLE_HEAP_TRACKER)

}  // namespace
}  // namespace goldfish::videobridge
