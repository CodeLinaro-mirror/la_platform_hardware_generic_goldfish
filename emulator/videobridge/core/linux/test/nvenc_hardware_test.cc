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
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/frame.h"
#include "libavutil/pixfmt.h"
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/environment/environment_factory.h"
#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/nv12_buffer.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "common_video/h264/h264_bitstream_parser.h"
#include "common_video/h264/h264_common.h"
#include "common_video/h264/pps_parser.h"
#include "common_video/h264/sps_parser.h"
#include "modules/video_coding/include/video_error_codes.h"
#pragma clang diagnostic pop

#include "core/linux/nvenc_loader.h"
#include "core/linux/nvenc_video_encoder.h"
#include "core/linux/nvenc_video_encoder_factory.h"
#include "goldfish/videobridge/codec_factories.h"

namespace goldfish::videobridge {
namespace {

using ::testing::Contains;
using ::testing::Not;

class CapturingEncodedImageCallback : public webrtc::EncodedImageCallback {
  public:
    Result OnEncodedImage(const webrtc::EncodedImage& encoded_image,
                          const webrtc::CodecSpecificInfo* codec_specific_info) override {
        frames_.push_back(encoded_image);
        if (codec_specific_info) {
            last_codec_info_ = *codec_specific_info;
        }
        return Result(Result::OK);
    }

    void OnFrameDropped(uint32_t /*rtp_timestamp*/, int /*spatial_id*/,
                        bool /*is_end_of_temporal_unit*/) override {}

    const std::vector<webrtc::EncodedImage>& Frames() const { return frames_; }
    void Clear() { frames_.clear(); }

  private:
    std::vector<webrtc::EncodedImage> frames_;
    webrtc::CodecSpecificInfo last_codec_info_{};
};

std::vector<webrtc::H264::NaluType> ParseAnnexBNaluTypes(const webrtc::EncodedImage& image) {
    std::span<const uint8_t> bitstream(image.data(), image.size());
    std::vector<webrtc::H264::NaluIndex> indices = webrtc::H264::FindNaluIndices(bitstream);
    std::vector<webrtc::H264::NaluType> types;
    types.reserve(indices.size());
    for (const auto& idx : indices) {
        if (idx.payload_size > 0) {
            types.push_back(webrtc::H264::ParseNaluType(bitstream[idx.payload_start_offset]));
        }
    }
    return types;
}

std::optional<webrtc::SpsParser::SpsState> ExtractSpsFromBitstream(
        const webrtc::EncodedImage& image) {
    std::span<const uint8_t> bitstream(image.data(), image.size());
    for (const auto& idx : webrtc::H264::FindNaluIndices(bitstream)) {
        if (idx.payload_size > 1 &&
            webrtc::H264::ParseNaluType(bitstream[idx.payload_start_offset]) ==
                    webrtc::H264::NaluType::kSps) {
            return webrtc::SpsParser::ParseSps(
                    bitstream.subspan(idx.payload_start_offset + 1, idx.payload_size - 1));
        }
    }
    return std::nullopt;
}

std::optional<webrtc::PpsParser::PpsState> ExtractPpsFromBitstream(
        const webrtc::EncodedImage& image) {
    std::span<const uint8_t> bitstream(image.data(), image.size());
    for (const auto& idx : webrtc::H264::FindNaluIndices(bitstream)) {
        if (idx.payload_size > 1 &&
            webrtc::H264::ParseNaluType(bitstream[idx.payload_start_offset]) ==
                    webrtc::H264::NaluType::kPps) {
            return webrtc::PpsParser::ParsePps(
                    bitstream.subspan(idx.payload_start_offset + 1, idx.payload_size - 1));
        }
    }
    return std::nullopt;
}

webrtc::VideoFrame CreatePatternedI420Frame(int width, int height, int frame_index,
                                            uint32_t rtp_timestamp) {
    webrtc::scoped_refptr<webrtc::I420Buffer> buffer = webrtc::I420Buffer::Create(width, height);
    uint8_t* y_plane = buffer->MutableDataY();
    for (int r = 0; r < height; ++r) {
        uint8_t* row = y_plane + static_cast<ptrdiff_t>(r) * buffer->StrideY();
        uint32_t by = static_cast<uint32_t>(r / 4);
        for (int c = 0; c < width; ++c) {
            // Moderate-amplitude (+/- 28 around 128) 4x4 block texture that quantizes down cleanly
            // at low bitrates (high QP) while preserving fine detail at high bitrates (low QP).
            uint32_t bx = static_cast<uint32_t>(c / 4);
            uint32_t hash = (bx * 2654435761U) ^ (by * 2246822519U) ^
                            (static_cast<uint32_t>(frame_index) * 3266489917U);
            int delta = static_cast<int>((hash >> 24) % 57) - 28;
            row[c] = static_cast<uint8_t>(128 + delta);
        }
    }

    uint8_t* u_plane = buffer->MutableDataU();
    uint8_t* v_plane = buffer->MutableDataV();
    for (int r = 0; r < height / 2; ++r) {
        uint8_t* u_row = u_plane + static_cast<ptrdiff_t>(r) * buffer->StrideU();
        uint8_t* v_row = v_plane + static_cast<ptrdiff_t>(r) * buffer->StrideV();
        for (int c = 0; c < width / 2; ++c) {
            u_row[c] = static_cast<uint8_t>(128 + (((c + r + frame_index) % 17) - 8));
            v_row[c] = static_cast<uint8_t>(128 + (((c - r + frame_index) % 17) - 8));
        }
    }

    return webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(buffer)
            .set_rtp_timestamp(rtp_timestamp)
            .set_timestamp_us(static_cast<int64_t>(frame_index) * 33333)
            .set_rotation(webrtc::kVideoRotation_0)
            .build();
}

webrtc::VideoFrame CreateSolidColorI420Frame(int width, int height, uint8_t y_val, uint8_t u_val,
                                             uint8_t v_val, uint32_t rtp_timestamp) {
    webrtc::scoped_refptr<webrtc::I420Buffer> buffer = webrtc::I420Buffer::Create(width, height);
    std::memset(buffer->MutableDataY(), y_val,
                static_cast<size_t>(buffer->StrideY()) * static_cast<size_t>(height));
    std::memset(buffer->MutableDataU(), u_val,
                static_cast<size_t>(buffer->StrideU()) * static_cast<size_t>(height / 2));
    std::memset(buffer->MutableDataV(), v_val,
                static_cast<size_t>(buffer->StrideV()) * static_cast<size_t>(height / 2));

    return webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(buffer)
            .set_rtp_timestamp(rtp_timestamp)
            .set_timestamp_us(static_cast<int64_t>(rtp_timestamp) * 10)
            .set_rotation(webrtc::kVideoRotation_0)
            .build();
}

class NvencHardwareTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        auto loader_or = NvencLoader::Create();
        if (loader_or.ok()) {
            shared_loader_ = std::move(*loader_or);
        } else {
            skip_reason_ = std::string(loader_or.status().message());
        }
    }

    static void TearDownTestSuite() { shared_loader_.reset(); }

    void SetUp() override {
        if (!shared_loader_) {
            GTEST_SKIP() << "Skipping NVIDIA hardware encoder test (no NVIDIA GPU/driver): "
                         << skip_reason_;
        }
    }

    static webrtc::VideoCodec MakeCodecSettings(uint16_t width, uint16_t height,
                                                uint32_t bitrate_kbps = 2000, uint32_t fps = 30) {
        webrtc::VideoCodec codec{};
        codec.codecType = webrtc::kVideoCodecH264;
        codec.width = width;
        codec.height = height;
        codec.startBitrate = bitrate_kbps;
        codec.maxBitrate = bitrate_kbps * 2;
        codec.minBitrate = 100;
        codec.maxFramerate = fps;
        return codec;
    }

    static webrtc::VideoEncoder::Settings DefaultEncoderSettings() {
        return webrtc::VideoEncoder::Settings(webrtc::VideoEncoder::Capabilities(false), 1, 1200);
    }

    static std::shared_ptr<NvencLoader> shared_loader_;
    static std::string skip_reason_;
};

std::shared_ptr<NvencLoader> NvencHardwareTest::shared_loader_ = nullptr;
std::string NvencHardwareTest::skip_reason_;

// 1. Validates dynamic loading of libcuda.so.1 + libnvidia-encode.so.1, CUDA context creation,
//    and raw NVENC hardware session lifecycle.
TEST_F(NvencHardwareTest, LoaderInitializesCudaContextAndNvencApi) {
    auto fresh_loader_or = NvencLoader::Create();
    ASSERT_TRUE(fresh_loader_or.ok()) << fresh_loader_or.status();
    std::unique_ptr<NvencLoader> loader = std::move(*fresh_loader_or);
    ASSERT_NE(loader, nullptr);
    EXPECT_NE(loader->cuda_context(), nullptr);

    const auto& api = loader->api();
    ASSERT_NE(api.nvEncOpenEncodeSessionEx, nullptr);
    ASSERT_NE(api.nvEncGetEncodeGUIDs, nullptr);
    ASSERT_NE(api.nvEncInitializeEncoder, nullptr);
    ASSERT_NE(api.nvEncEncodePicture, nullptr);
    ASSERT_NE(api.nvEncDestroyEncoder, nullptr);

    auto session_params = MakeNvencStruct<NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS>();
    session_params.apiVersion = NVENCAPI_VERSION;
    session_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
    session_params.device = loader->cuda_context();

    void* session = nullptr;
    NVENCSTATUS status = api.nvEncOpenEncodeSessionEx(&session_params, &session);
    ASSERT_EQ(status, NV_ENC_SUCCESS) << NvencStatusToString(status);
    ASSERT_NE(session, nullptr);

    EXPECT_EQ(api.nvEncDestroyEncoder(session), NV_ENC_SUCCESS);
}

// 2. Validates NvencVideoEncoderFactory hardware capability probing on the physical NVIDIA GPU.
TEST_F(NvencHardwareTest, FactoryProbesSupportedH264Profiles) {
    auto factory = NvencVideoEncoderFactory::CreateForTest(shared_loader_);
    ASSERT_NE(factory, nullptr);

    std::vector<webrtc::SdpVideoFormat> formats = factory->GetSupportedFormats();
    EXPECT_FALSE(formats.empty());

    bool has_constrained_baseline = false;
    bool has_main = false;
    bool has_high = false;
    bool has_packetization_mode_1 = false;
    bool has_packetization_mode_0 = false;

    for (const auto& fmt : formats) {
        EXPECT_EQ(fmt.name, "H264");
        auto profile_it = fmt.parameters.find("profile-level-id");
        if (profile_it != fmt.parameters.end()) {
            if (profile_it->second == "42e01f") has_constrained_baseline = true;
            if (profile_it->second == "4d001f") has_main = true;
            if (profile_it->second == "640c1f") has_high = true;
        }
        auto pkt_it = fmt.parameters.find("packetization-mode");
        if (pkt_it != fmt.parameters.end()) {
            if (pkt_it->second == "1") has_packetization_mode_1 = true;
            if (pkt_it->second == "0") has_packetization_mode_0 = true;
        }
    }

    EXPECT_TRUE(has_constrained_baseline);
    EXPECT_TRUE(has_main);
    EXPECT_TRUE(has_high);
    EXPECT_TRUE(has_packetization_mode_1);
    EXPECT_TRUE(has_packetization_mode_0);
}

// 3. Validates initializing NvencVideoEncoder across Baseline, Main, and High H.264 profiles
//    and standard display resolutions on the GPU.
TEST_F(NvencHardwareTest, InitEncodeAcrossProfilesAndResolutions) {
    const std::vector<std::string> profile_level_ids = {"42e01f", "4d001f", "640c1f"};
    const std::vector<std::pair<uint16_t, uint16_t>> resolutions = {
        {640, 480},
        {1280, 720},
        {1920, 1080},
    };

    for (const auto& profile_id : profile_level_ids) {
        webrtc::SdpVideoFormat format("H264", {{"profile-level-id", profile_id}});
        auto encoder = NvencVideoEncoder::Create(shared_loader_, format);
        ASSERT_NE(encoder, nullptr);

        webrtc::VideoEncoder::EncoderInfo info = encoder->GetEncoderInfo();
        EXPECT_TRUE(info.is_hardware_accelerated);

        for (const auto& [width, height] : resolutions) {
            webrtc::VideoCodec settings = MakeCodecSettings(width, height);
            EXPECT_EQ(encoder->InitEncode(&settings, DefaultEncoderSettings()),
                      WEBRTC_VIDEO_CODEC_OK)
                    << "Failed InitEncode for profile " << profile_id << " at " << width << "x"
                    << height;
            EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
        }
    }
}

// 4. Validates encoding I420, contiguous NV12, and padded-stride NV12 frames on the GPU
//    and verifies the emitted Annex B H.264 bitstream NALU structure.
TEST_F(NvencHardwareTest, EncodesI420AndNv12ToValidAnnexBBitstream) {
    constexpr uint16_t kWidth = 640;
    constexpr uint16_t kHeight = 360;

    auto encoder = NvencVideoEncoder::Create(shared_loader_);
    ASSERT_NE(encoder, nullptr);

    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoCodec codec = MakeCodecSettings(kWidth, kHeight, 2000, 30);
    ASSERT_EQ(encoder->InitEncode(&codec, DefaultEncoderSettings()), WEBRTC_VIDEO_CODEC_OK);

    // Frame 0: I420 keyframe
    std::vector<webrtc::VideoFrameType> key_request = {webrtc::VideoFrameType::kVideoFrameKey};
    webrtc::VideoFrame i420_frame = CreatePatternedI420Frame(kWidth, kHeight, 0, 90000);
    ASSERT_EQ(encoder->Encode(i420_frame, &key_request), WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(callback.Frames().size(), 1U);

    const webrtc::EncodedImage& key_image = callback.Frames()[0];
    EXPECT_EQ(key_image._frameType, webrtc::VideoFrameType::kVideoFrameKey);
    EXPECT_EQ(key_image._encodedWidth, kWidth);
    EXPECT_EQ(key_image._encodedHeight, kHeight);
    EXPECT_EQ(key_image.RtpTimestamp(), 90000U);
    EXPECT_GT(key_image.size(), 0U);

    std::vector<webrtc::H264::NaluType> key_nalus = ParseAnnexBNaluTypes(key_image);
    EXPECT_THAT(key_nalus, Contains(webrtc::H264::NaluType::kSps));
    EXPECT_THAT(key_nalus, Contains(webrtc::H264::NaluType::kPps));
    EXPECT_THAT(key_nalus, Contains(webrtc::H264::NaluType::kIdr));

    auto sps = ExtractSpsFromBitstream(key_image);
    ASSERT_TRUE(sps.has_value());
    EXPECT_EQ(sps->width, kWidth);
    EXPECT_EQ(sps->height, kHeight);
    EXPECT_TRUE(ExtractPpsFromBitstream(key_image).has_value());

    // Frame 1: Contiguous NV12 delta frame
    webrtc::scoped_refptr<webrtc::NV12Buffer> nv12_contiguous =
            webrtc::NV12Buffer::Create(kWidth, kHeight);
    std::memset(nv12_contiguous->MutableDataY(), 140,
                static_cast<size_t>(nv12_contiguous->StrideY()) * kHeight);
    std::memset(nv12_contiguous->MutableDataUV(), 90,
                static_cast<size_t>(nv12_contiguous->StrideUV()) * (kHeight / 2));
    webrtc::VideoFrame nv12_frame = webrtc::VideoFrame::Builder()
                                            .set_video_frame_buffer(nv12_contiguous)
                                            .set_rtp_timestamp(93000)
                                            .build();

    std::vector<webrtc::VideoFrameType> delta_request = {webrtc::VideoFrameType::kVideoFrameDelta};
    ASSERT_EQ(encoder->Encode(nv12_frame, &delta_request), WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(callback.Frames().size(), 2U);

    const webrtc::EncodedImage& delta_image = callback.Frames()[1];
    EXPECT_EQ(delta_image._frameType, webrtc::VideoFrameType::kVideoFrameDelta);
    std::vector<webrtc::H264::NaluType> delta_nalus = ParseAnnexBNaluTypes(delta_image);
    EXPECT_THAT(delta_nalus, Contains(webrtc::H264::NaluType::kSlice));
    EXPECT_THAT(delta_nalus, Not(Contains(webrtc::H264::NaluType::kIdr)));

    // Frame 2: Non-contiguous / padded-stride NV12 frame (stride = 704 > width = 640)
    webrtc::scoped_refptr<webrtc::NV12Buffer> nv12_padded =
            webrtc::NV12Buffer::Create(kWidth, kHeight, 704, 704);
    std::memset(nv12_padded->MutableDataY(), 200,
                static_cast<size_t>(nv12_padded->StrideY()) * kHeight);
    std::memset(nv12_padded->MutableDataUV(), 160,
                static_cast<size_t>(nv12_padded->StrideUV()) * (kHeight / 2));
    webrtc::VideoFrame padded_frame = webrtc::VideoFrame::Builder()
                                              .set_video_frame_buffer(nv12_padded)
                                              .set_rtp_timestamp(96000)
                                              .build();

    ASSERT_EQ(encoder->Encode(padded_frame, &delta_request), WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(callback.Frames().size(), 3U);
    EXPECT_EQ(callback.Frames()[2]._frameType, webrtc::VideoFrameType::kVideoFrameDelta);
    EXPECT_THAT(ParseAnnexBNaluTypes(callback.Frames()[2]),
                Contains(webrtc::H264::NaluType::kSlice));

    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 5. Validates infinite GOP behavior and on-demand IDR generation with repeated SPS/PPS
//    when a mid-stream keyframe (PLI/FIR) is requested.
TEST_F(NvencHardwareTest, ForcesIdrOnMidStreamKeyframeRequest) {
    constexpr uint16_t kWidth = 640;
    constexpr uint16_t kHeight = 360;

    auto encoder = NvencVideoEncoder::Create(shared_loader_);
    ASSERT_NE(encoder, nullptr);

    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoCodec codec = MakeCodecSettings(kWidth, kHeight, 2000, 30);
    ASSERT_EQ(encoder->InitEncode(&codec, DefaultEncoderSettings()), WEBRTC_VIDEO_CODEC_OK);

    std::vector<webrtc::VideoFrameType> key_request = {webrtc::VideoFrameType::kVideoFrameKey};
    std::vector<webrtc::VideoFrameType> delta_request = {webrtc::VideoFrameType::kVideoFrameDelta};

    // Frame 0: initial keyframe
    ASSERT_EQ(encoder->Encode(CreatePatternedI420Frame(kWidth, kHeight, 0, 1000), &key_request),
              WEBRTC_VIDEO_CODEC_OK);

    // Frames 1..3: delta frames
    for (int i = 1; i <= 3; ++i) {
        ASSERT_EQ(encoder->Encode(CreatePatternedI420Frame(kWidth, kHeight, i, 1000 + i * 3000),
                                  &delta_request),
                  WEBRTC_VIDEO_CODEC_OK);
    }

    // Frame 4: mid-stream PLI keyframe request
    ASSERT_EQ(encoder->Encode(CreatePatternedI420Frame(kWidth, kHeight, 4, 13000), &key_request),
              WEBRTC_VIDEO_CODEC_OK);

    // Frame 5: delta frame after forced IDR
    ASSERT_EQ(encoder->Encode(CreatePatternedI420Frame(kWidth, kHeight, 5, 16000), &delta_request),
              WEBRTC_VIDEO_CODEC_OK);

    ASSERT_EQ(callback.Frames().size(), 6U);
    EXPECT_EQ(callback.Frames()[0]._frameType, webrtc::VideoFrameType::kVideoFrameKey);
    EXPECT_EQ(callback.Frames()[1]._frameType, webrtc::VideoFrameType::kVideoFrameDelta);
    EXPECT_EQ(callback.Frames()[2]._frameType, webrtc::VideoFrameType::kVideoFrameDelta);
    EXPECT_EQ(callback.Frames()[3]._frameType, webrtc::VideoFrameType::kVideoFrameDelta);
    EXPECT_EQ(callback.Frames()[4]._frameType, webrtc::VideoFrameType::kVideoFrameKey);
    EXPECT_EQ(callback.Frames()[5]._frameType, webrtc::VideoFrameType::kVideoFrameDelta);

    // Forced mid-stream IDR must include repeated SPS, PPS, and IDR NALUs for WebRTC decoder
    // recovery.
    std::vector<webrtc::H264::NaluType> forced_idr_nalus =
            ParseAnnexBNaluTypes(callback.Frames()[4]);
    EXPECT_THAT(forced_idr_nalus, Contains(webrtc::H264::NaluType::kSps));
    EXPECT_THAT(forced_idr_nalus, Contains(webrtc::H264::NaluType::kPps));
    EXPECT_THAT(forced_idr_nalus, Contains(webrtc::H264::NaluType::kIdr));

    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 6. Validates dynamic rate control reconfiguration (SetRates) on the physical GPU.
TEST_F(NvencHardwareTest, DynamicRateControlReconfiguration) {
    constexpr uint16_t kWidth = 1280;
    constexpr uint16_t kHeight = 720;
    constexpr int kFramesPerPhase = 15;

    auto encoder = NvencVideoEncoder::Create(shared_loader_);
    ASSERT_NE(encoder, nullptr);

    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    // Start at low bitrate: 300 kbps @ 30 fps
    webrtc::VideoCodec codec = MakeCodecSettings(kWidth, kHeight, 300, 30);
    ASSERT_EQ(encoder->InitEncode(&codec, DefaultEncoderSettings()), WEBRTC_VIDEO_CODEC_OK);

    std::vector<webrtc::VideoFrameType> key_request = {webrtc::VideoFrameType::kVideoFrameKey};
    std::vector<webrtc::VideoFrameType> delta_request = {webrtc::VideoFrameType::kVideoFrameDelta};

    // Warm up with Frame 0 (initial IDR keyframe) so both rate phases measure P-frames equally.
    ASSERT_EQ(encoder->Encode(CreatePatternedI420Frame(kWidth, kHeight, 0, 1000), &key_request),
              WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(callback.Frames().size(), 1U);

    webrtc::H264BitstreamParser bitstream_parser;
    bitstream_parser.ParseBitstream(
            std::span<const uint8_t>(callback.Frames()[0].data(), callback.Frames()[0].size()));
    callback.Clear();

    size_t low_bitrate_bytes = 0;
    std::optional<int> low_bitrate_qp;
    for (int i = 1; i <= kFramesPerPhase; ++i) {
        ASSERT_EQ(encoder->Encode(CreatePatternedI420Frame(kWidth, kHeight, i, 1000 + i * 3000),
                                  &delta_request),
                  WEBRTC_VIDEO_CODEC_OK);
    }
    ASSERT_EQ(callback.Frames().size(), static_cast<size_t>(kFramesPerPhase));
    for (const auto& img : callback.Frames()) {
        low_bitrate_bytes += img.size();
        bitstream_parser.ParseBitstream(std::span<const uint8_t>(img.data(), img.size()));
        low_bitrate_qp = bitstream_parser.GetLastSliceQp();
    }
    ASSERT_TRUE(low_bitrate_qp.has_value());

    // Reconfigure mid-stream to high bitrate: 15 Mbps @ 30 fps
    callback.Clear();
    webrtc::VideoEncoder::RateControlParameters high_rates;
    high_rates.bitrate.SetBitrate(0, 0, 15000000);
    high_rates.framerate_fps = 30.0;
    encoder->SetRates(high_rates);

    size_t high_bitrate_bytes = 0;
    std::optional<int> high_bitrate_qp;
    for (int i = 1; i <= kFramesPerPhase; ++i) {
        int idx = kFramesPerPhase + i;
        ASSERT_EQ(encoder->Encode(CreatePatternedI420Frame(kWidth, kHeight, idx, 1000 + idx * 3000),
                                  &delta_request),
                  WEBRTC_VIDEO_CODEC_OK);
    }
    ASSERT_EQ(callback.Frames().size(), static_cast<size_t>(kFramesPerPhase));
    for (const auto& img : callback.Frames()) {
        high_bitrate_bytes += img.size();
        bitstream_parser.ParseBitstream(std::span<const uint8_t>(img.data(), img.size()));
        high_bitrate_qp = bitstream_parser.GetLastSliceQp();
    }
    ASSERT_TRUE(high_bitrate_qp.has_value());

    EXPECT_GT(high_bitrate_bytes, low_bitrate_bytes * 2)
            << "Expected 15 Mbps P-frames (" << high_bitrate_bytes
            << " bytes) to be >2x larger than 300 kbps P-frames (" << low_bitrate_bytes
            << " bytes)";
    EXPECT_LT(*high_bitrate_qp, *low_bitrate_qp)
            << "Expected 15 Mbps slice QP (" << *high_bitrate_qp
            << ") to be lower than 300 kbps slice QP (" << *low_bitrate_qp << ")";

    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 7. Validates re-initializing the same encoder instance across multiple resolution changes.
TEST_F(NvencHardwareTest, ReinitializesOnResolutionChangeWithoutLeaks) {
    auto encoder = NvencVideoEncoder::Create(shared_loader_);
    ASSERT_NE(encoder, nullptr);

    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    const std::vector<std::pair<uint16_t, uint16_t>> resolutions = {
        {640, 480},
        {1280, 720},
        {1920, 1080},
        {640, 360},
    };

    std::vector<webrtc::VideoFrameType> key_request = {webrtc::VideoFrameType::kVideoFrameKey};
    for (size_t i = 0; i < resolutions.size(); ++i) {
        const auto [width, height] = resolutions[i];
        callback.Clear();

        webrtc::VideoCodec codec = MakeCodecSettings(width, height, 3000, 30);
        ASSERT_EQ(encoder->InitEncode(&codec, DefaultEncoderSettings()), WEBRTC_VIDEO_CODEC_OK);

        webrtc::VideoFrame frame = CreatePatternedI420Frame(width, height, static_cast<int>(i),
                                                            static_cast<uint32_t>((i + 1) * 90000));
        ASSERT_EQ(encoder->Encode(frame, &key_request), WEBRTC_VIDEO_CODEC_OK);
        ASSERT_EQ(callback.Frames().size(), 1U);
        EXPECT_EQ(callback.Frames()[0]._encodedWidth, width);
        EXPECT_EQ(callback.Frames()[0]._encodedHeight, height);

        auto sps = ExtractSpsFromBitstream(callback.Frames()[0]);
        ASSERT_TRUE(sps.has_value());
        EXPECT_EQ(sps->width, width);
        EXPECT_EQ(sps->height, height);
    }

    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 8. End-to-end hardware encode to software H.264 decode loopback: encodes distinct YUV color
//    frames on the NVIDIA GPU, parses slice QP via WebRTC's H264BitstreamParser, and decodes
//    the Annex B bitstream via FFmpeg's H.264 decoder to verify pixel fidelity.
TEST_F(NvencHardwareTest, EndToEndHardwareEncodeToSoftwareDecodeLoopback) {
    constexpr uint16_t kWidth = 640;
    constexpr uint16_t kHeight = 360;
    constexpr uint8_t kTargetY = 180;
    constexpr uint8_t kTargetU = 70;
    constexpr uint8_t kTargetV = 200;

    auto encoder = NvencVideoEncoder::Create(
            shared_loader_, webrtc::SdpVideoFormat("H264", {{"profile-level-id", "640c1f"}}));
    ASSERT_NE(encoder, nullptr);

    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoCodec codec = MakeCodecSettings(kWidth, kHeight, 4000, 30);
    ASSERT_EQ(encoder->InitEncode(&codec, DefaultEncoderSettings()), WEBRTC_VIDEO_CODEC_OK);

    std::vector<webrtc::VideoFrameType> key_request = {webrtc::VideoFrameType::kVideoFrameKey};
    std::vector<webrtc::VideoFrameType> delta_request = {webrtc::VideoFrameType::kVideoFrameDelta};

    ASSERT_EQ(encoder->Encode(CreateSolidColorI420Frame(kWidth, kHeight, kTargetY, kTargetU,
                                                        kTargetV, 90000),
                              &key_request),
              WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(encoder->Encode(CreateSolidColorI420Frame(kWidth, kHeight, kTargetY, kTargetU,
                                                        kTargetV, 93000),
                              &delta_request),
              WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(callback.Frames().size(), 2U);

    // 1. Verify WebRTC's H264BitstreamParser parses both IDR and P frames and extracts valid QPs.
    webrtc::H264BitstreamParser bitstream_parser;
    for (const auto& encoded_frame : callback.Frames()) {
        bitstream_parser.ParseBitstream(
                std::span<const uint8_t>(encoded_frame.data(), encoded_frame.size()));
        std::optional<int> qp = bitstream_parser.GetLastSliceQp();
        ASSERT_TRUE(qp.has_value());
        EXPECT_GE(*qp, 0);
        EXPECT_LE(*qp, 51);
    }

    // 2. Decode the GPU-encoded H.264 bitstream with FFmpeg's H.264 software decoder and verify
    //    reconstructed YUV pixel values.
    const AVCodec* h264_decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    ASSERT_NE(h264_decoder, nullptr);

    std::unique_ptr<AVCodecContext, void (*)(AVCodecContext*)> dec_ctx(
            avcodec_alloc_context3(h264_decoder),
            [](AVCodecContext* ctx) { avcodec_free_context(&ctx); });
    ASSERT_NE(dec_ctx, nullptr);
    ASSERT_GE(avcodec_open2(dec_ctx.get(), h264_decoder, nullptr), 0);

    std::unique_ptr<AVPacket, void (*)(AVPacket*)> pkt(av_packet_alloc(),
                                                       [](AVPacket* p) { av_packet_free(&p); });
    std::unique_ptr<AVFrame, void (*)(AVFrame*)> decoded_frame(
            av_frame_alloc(), [](AVFrame* f) { av_frame_free(&f); });
    ASSERT_NE(pkt, nullptr);
    ASSERT_NE(decoded_frame, nullptr);

    int decoded_count = 0;
    auto drain_decoded_frames = [&]() {
        while (avcodec_receive_frame(dec_ctx.get(), decoded_frame.get()) == 0) {
            ++decoded_count;
            EXPECT_EQ(decoded_frame->width, kWidth);
            EXPECT_EQ(decoded_frame->height, kHeight);
            EXPECT_EQ(decoded_frame->format, AV_PIX_FMT_YUV420P);

            // Sample center pixel in Y, U, V planes and verify within lossy H.264 tolerance (+/- 5)
            uint8_t center_y =
                    decoded_frame
                            ->data[0][(kHeight / 2) * decoded_frame->linesize[0] + (kWidth / 2)];
            uint8_t center_u =
                    decoded_frame
                            ->data[1][(kHeight / 4) * decoded_frame->linesize[1] + (kWidth / 4)];
            uint8_t center_v =
                    decoded_frame
                            ->data[2][(kHeight / 4) * decoded_frame->linesize[2] + (kWidth / 4)];

            EXPECT_NEAR(center_y, kTargetY, 5);
            EXPECT_NEAR(center_u, kTargetU, 5);
            EXPECT_NEAR(center_v, kTargetV, 5);
        }
    };

    for (const auto& encoded_frame : callback.Frames()) {
        pkt->data = const_cast<uint8_t*>(encoded_frame.data());
        pkt->size = static_cast<int>(encoded_frame.size());
        ASSERT_GE(avcodec_send_packet(dec_ctx.get(), pkt.get()), 0);
        drain_decoded_frames();
    }

    // Flush decoder
    ASSERT_GE(avcodec_send_packet(dec_ctx.get(), nullptr), 0);
    drain_decoded_frames();

    EXPECT_EQ(decoded_count, 2);
    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 9. Validates that 4-quadrant colors (Red, Green, Blue, Yellow) encoded from both I420 and NV12
//    buffers decode at their exact spatial locations without UV channel swap (NV12 vs NV21),
//    stride skew, or vertical flip.
TEST_F(NvencHardwareTest, PreservesSpatialQuadrantColorsAndOrientation) {
    constexpr uint16_t kWidth = 640;
    constexpr uint16_t kHeight = 360;

    struct YuvColor {
        uint8_t y;
        uint8_t u;
        uint8_t v;
    };
    // Top-Left: Red, Top-Right: Green, Bottom-Left: Blue, Bottom-Right: Yellow
    constexpr YuvColor kTopLeft{82, 90, 240};
    constexpr YuvColor kTopRight{145, 54, 34};
    constexpr YuvColor kBottomLeft{41, 240, 110};
    constexpr YuvColor kBottomRight{210, 16, 146};

    auto pick_quadrant = [&](int x, int y) -> YuvColor {
        bool left = (x < kWidth / 2);
        bool top = (y < kHeight / 2);
        if (top && left) return kTopLeft;
        if (top && !left) return kTopRight;
        if (!top && left) return kBottomLeft;
        return kBottomRight;
    };

    // Build I420 quadrant frame
    webrtc::scoped_refptr<webrtc::I420Buffer> i420_buf =
            webrtc::I420Buffer::Create(kWidth, kHeight);
    for (int r = 0; r < kHeight; ++r) {
        for (int c = 0; c < kWidth; ++c) {
            i420_buf->MutableDataY()[r * i420_buf->StrideY() + c] = pick_quadrant(c, r).y;
        }
    }
    for (int r = 0; r < kHeight / 2; ++r) {
        for (int c = 0; c < kWidth / 2; ++c) {
            YuvColor color = pick_quadrant(c * 2, r * 2);
            i420_buf->MutableDataU()[r * i420_buf->StrideU() + c] = color.u;
            i420_buf->MutableDataV()[r * i420_buf->StrideV() + c] = color.v;
        }
    }

    // Build NV12 quadrant frame
    webrtc::scoped_refptr<webrtc::NV12Buffer> nv12_buf =
            webrtc::NV12Buffer::Create(kWidth, kHeight);
    for (int r = 0; r < kHeight; ++r) {
        for (int c = 0; c < kWidth; ++c) {
            nv12_buf->MutableDataY()[r * nv12_buf->StrideY() + c] = pick_quadrant(c, r).y;
        }
    }
    for (int r = 0; r < kHeight / 2; ++r) {
        for (int c = 0; c < kWidth / 2; ++c) {
            YuvColor color = pick_quadrant(c * 2, r * 2);
            nv12_buf->MutableDataUV()[r * nv12_buf->StrideUV() + c * 2] = color.u;
            nv12_buf->MutableDataUV()[r * nv12_buf->StrideUV() + c * 2 + 1] = color.v;
        }
    }

    auto encoder = NvencVideoEncoder::Create(shared_loader_);
    ASSERT_NE(encoder, nullptr);
    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoCodec codec = MakeCodecSettings(kWidth, kHeight, 6000, 30);
    ASSERT_EQ(encoder->InitEncode(&codec, DefaultEncoderSettings()), WEBRTC_VIDEO_CODEC_OK);

    std::vector<webrtc::VideoFrameType> key_request = {webrtc::VideoFrameType::kVideoFrameKey};
    ASSERT_EQ(encoder->Encode(webrtc::VideoFrame::Builder()
                                      .set_video_frame_buffer(i420_buf)
                                      .set_rtp_timestamp(90000)
                                      .build(),
                              &key_request),
              WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(encoder->Encode(webrtc::VideoFrame::Builder()
                                      .set_video_frame_buffer(nv12_buf)
                                      .set_rtp_timestamp(93000)
                                      .build(),
                              &key_request),
              WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(callback.Frames().size(), 2U);

    const AVCodec* h264_decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    ASSERT_NE(h264_decoder, nullptr);
    std::unique_ptr<AVCodecContext, void (*)(AVCodecContext*)> dec_ctx(
            avcodec_alloc_context3(h264_decoder),
            [](AVCodecContext* ctx) { avcodec_free_context(&ctx); });
    ASSERT_GE(avcodec_open2(dec_ctx.get(), h264_decoder, nullptr), 0);

    std::unique_ptr<AVPacket, void (*)(AVPacket*)> pkt(av_packet_alloc(),
                                                       [](AVPacket* p) { av_packet_free(&p); });
    std::unique_ptr<AVFrame, void (*)(AVFrame*)> decoded(av_frame_alloc(),
                                                         [](AVFrame* f) { av_frame_free(&f); });

    auto verify_quadrant = [&](const AVFrame* f, int sample_x, int sample_y, YuvColor expected) {
        uint8_t y = f->data[0][sample_y * f->linesize[0] + sample_x];
        uint8_t u = f->data[1][(sample_y / 2) * f->linesize[1] + (sample_x / 2)];
        uint8_t v = f->data[2][(sample_y / 2) * f->linesize[2] + (sample_x / 2)];
        EXPECT_NEAR(y, expected.y, 6);
        EXPECT_NEAR(u, expected.u, 6);
        EXPECT_NEAR(v, expected.v, 6);
    };

    int verified_frames = 0;
    for (const auto& encoded_frame : callback.Frames()) {
        pkt->data = const_cast<uint8_t*>(encoded_frame.data());
        pkt->size = static_cast<int>(encoded_frame.size());
        ASSERT_GE(avcodec_send_packet(dec_ctx.get(), pkt.get()), 0);
        while (avcodec_receive_frame(dec_ctx.get(), decoded.get()) == 0) {
            ++verified_frames;
            verify_quadrant(decoded.get(), kWidth / 4, kHeight / 4, kTopLeft);
            verify_quadrant(decoded.get(), (kWidth * 3) / 4, kHeight / 4, kTopRight);
            verify_quadrant(decoded.get(), kWidth / 4, (kHeight * 3) / 4, kBottomLeft);
            verify_quadrant(decoded.get(), (kWidth * 3) / 4, (kHeight * 3) / 4, kBottomRight);
        }
    }
    ASSERT_GE(avcodec_send_packet(dec_ctx.get(), nullptr), 0);
    while (avcodec_receive_frame(dec_ctx.get(), decoded.get()) == 0) {
        ++verified_frames;
        verify_quadrant(decoded.get(), kWidth / 4, kHeight / 4, kTopLeft);
        verify_quadrant(decoded.get(), (kWidth * 3) / 4, kHeight / 4, kTopRight);
        verify_quadrant(decoded.get(), kWidth / 4, (kHeight * 3) / 4, kBottomLeft);
        verify_quadrant(decoded.get(), (kWidth * 3) / 4, (kHeight * 3) / 4, kBottomRight);
    }
    EXPECT_EQ(verified_frames, 2);
    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 10. Validates that the raw H.264 SPS NALU profile_idc byte emitted by the GPU matches
//     the SDP-negotiated profile-level-id (Baseline=66, Main=77, High=100).
TEST_F(NvencHardwareTest, BitstreamSpsMatchesNegotiatedProfileIdc) {
    struct ProfileTestCase {
        std::string profile_level_id;
        uint8_t expected_profile_idc;
    };
    const std::vector<ProfileTestCase> cases = {
        {"42e01f", 66},   // Constrained Baseline (0x42)
        {"4d001f", 77},   // Main (0x4d)
        {"640c1f", 100},  // High (0x64)
    };

    for (const auto& tc : cases) {
        webrtc::SdpVideoFormat format("H264", {{"profile-level-id", tc.profile_level_id}});
        auto encoder = NvencVideoEncoder::Create(shared_loader_, format);
        ASSERT_NE(encoder, nullptr);

        CapturingEncodedImageCallback callback;
        ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

        webrtc::VideoCodec codec = MakeCodecSettings(640, 360, 2000, 30);
        ASSERT_EQ(encoder->InitEncode(&codec, DefaultEncoderSettings()), WEBRTC_VIDEO_CODEC_OK);

        std::vector<webrtc::VideoFrameType> key_request = {webrtc::VideoFrameType::kVideoFrameKey};
        ASSERT_EQ(encoder->Encode(CreatePatternedI420Frame(640, 360, 0, 90000), &key_request),
                  WEBRTC_VIDEO_CODEC_OK);
        ASSERT_EQ(callback.Frames().size(), 1U);

        std::span<const uint8_t> bitstream(callback.Frames()[0].data(),
                                           callback.Frames()[0].size());
        std::optional<uint8_t> found_profile_idc;
        for (const auto& idx : webrtc::H264::FindNaluIndices(bitstream)) {
            if (idx.payload_size >= 4 &&
                webrtc::H264::ParseNaluType(bitstream[idx.payload_start_offset]) ==
                        webrtc::H264::NaluType::kSps) {
                found_profile_idc = bitstream[idx.payload_start_offset + 1];
                break;
            }
        }
        ASSERT_TRUE(found_profile_idc.has_value());
        EXPECT_EQ(*found_profile_idc, tc.expected_profile_idc)
                << "Mismatch for SDP profile-level-id " << tc.profile_level_id;

        EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
    }
}

// 11. Validates encoding portrait, foldable, wearable, and non-multiple-of-16 emulator
//     resolutions (verifying H.264 SPS frame_cropping_flag crops 1088 macroblock height/width
//     back to 1080).
TEST_F(NvencHardwareTest, EncodesPortraitAndFoldableEmulatorResolutions) {
    const std::vector<std::pair<uint16_t, uint16_t>> emulator_resolutions = {
        {720, 1600},   // Portrait phone
        {1080, 2400},  // High-res portrait phone (1080 is not divisible by 16)
        {1840, 2208},  // Foldable unfolded display
        {384, 384},    // Wear OS square display
    };

    auto encoder = NvencVideoEncoder::Create(shared_loader_);
    ASSERT_NE(encoder, nullptr);

    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    std::vector<webrtc::VideoFrameType> key_request = {webrtc::VideoFrameType::kVideoFrameKey};
    for (const auto& [width, height] : emulator_resolutions) {
        callback.Clear();
        webrtc::VideoCodec codec = MakeCodecSettings(width, height, 4000, 30);
        ASSERT_EQ(encoder->InitEncode(&codec, DefaultEncoderSettings()), WEBRTC_VIDEO_CODEC_OK)
                << "Failed InitEncode at " << width << "x" << height;

        ASSERT_EQ(encoder->Encode(CreatePatternedI420Frame(width, height, 0, 90000), &key_request),
                  WEBRTC_VIDEO_CODEC_OK);
        ASSERT_EQ(callback.Frames().size(), 1U);

        // Verify SPS parser applies frame_cropping_flag and reports exact width x height (e.g. 1080
        // instead of macroblock-padded 1088).
        auto sps = ExtractSpsFromBitstream(callback.Frames()[0]);
        ASSERT_TRUE(sps.has_value());
        EXPECT_EQ(sps->width, width);
        EXPECT_EQ(sps->height, height);
    }

    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 12. Validates thread safety and CUDA context portability when InitEncode(), Encode()/SetRates(),
//     and Release() are invoked from distinct worker threads (matching WebRTC's signaling vs
//     EncoderQueue threading model).
TEST_F(NvencHardwareTest, CrossThreadInitEncodeAndRelease) {
    auto encoder = NvencVideoEncoder::Create(shared_loader_);
    ASSERT_NE(encoder, nullptr);

    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    // Thread A: InitEncode
    int32_t init_result = WEBRTC_VIDEO_CODEC_ERROR;
    std::thread init_thread([&]() {
        webrtc::VideoCodec codec = MakeCodecSettings(640, 360, 2000, 30);
        init_result = encoder->InitEncode(&codec, DefaultEncoderSettings());
    });
    init_thread.join();
    ASSERT_EQ(init_result, WEBRTC_VIDEO_CODEC_OK);

    // Thread B: Encode + SetRates + Encode
    int32_t encode1_result = WEBRTC_VIDEO_CODEC_ERROR;
    int32_t encode2_result = WEBRTC_VIDEO_CODEC_ERROR;
    std::thread encode_thread([&]() {
        std::vector<webrtc::VideoFrameType> key_req = {webrtc::VideoFrameType::kVideoFrameKey};
        std::vector<webrtc::VideoFrameType> delta_req = {webrtc::VideoFrameType::kVideoFrameDelta};
        encode1_result = encoder->Encode(CreatePatternedI420Frame(640, 360, 0, 90000), &key_req);

        webrtc::VideoEncoder::RateControlParameters rates;
        rates.bitrate.SetBitrate(0, 0, 4000000);
        rates.framerate_fps = 60.0;
        encoder->SetRates(rates);

        encode2_result = encoder->Encode(CreatePatternedI420Frame(640, 360, 1, 93000), &delta_req);
    });
    encode_thread.join();
    EXPECT_EQ(encode1_result, WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(encode2_result, WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(callback.Frames().size(), 2U);

    // Thread C: Release
    int32_t release_result = WEBRTC_VIDEO_CODEC_ERROR;
    std::thread release_thread([&]() { release_result = encoder->Release(); });
    release_thread.join();
    EXPECT_EQ(release_result, WEBRTC_VIDEO_CODEC_OK);
}

// 13. Validates zero B-frame reordering and immediate 1-in-1-out low-latency frame delivery:
//     each Encode() call emits its bitstream synchronously and decodes in strict presentation
//     order.
TEST_F(NvencHardwareTest, VerifiesZeroBFramesAndStrictDecodeOrder) {
    constexpr uint16_t kWidth = 640;
    constexpr uint16_t kHeight = 360;
    constexpr int kNumFrames = 8;

    auto encoder = NvencVideoEncoder::Create(shared_loader_);
    ASSERT_NE(encoder, nullptr);

    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoCodec codec = MakeCodecSettings(kWidth, kHeight, 4000, 30);
    ASSERT_EQ(encoder->InitEncode(&codec, DefaultEncoderSettings()), WEBRTC_VIDEO_CODEC_OK);

    const AVCodec* h264_decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    ASSERT_NE(h264_decoder, nullptr);
    std::unique_ptr<AVCodecContext, void (*)(AVCodecContext*)> dec_ctx(
            avcodec_alloc_context3(h264_decoder),
            [](AVCodecContext* ctx) { avcodec_free_context(&ctx); });
    dec_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    ASSERT_GE(avcodec_open2(dec_ctx.get(), h264_decoder, nullptr), 0);

    std::unique_ptr<AVPacket, void (*)(AVPacket*)> pkt(av_packet_alloc(),
                                                       [](AVPacket* p) { av_packet_free(&p); });
    std::unique_ptr<AVFrame, void (*)(AVFrame*)> decoded(av_frame_alloc(),
                                                         [](AVFrame* f) { av_frame_free(&f); });

    std::vector<webrtc::VideoFrameType> delta_req = {webrtc::VideoFrameType::kVideoFrameDelta};
    for (int i = 0; i < kNumFrames; ++i) {
        // Place a bright vertical bar (Y=230) centered at bar_center_x = 48 + i * 64
        int bar_center_x = 48 + i * 64;
        webrtc::scoped_refptr<webrtc::I420Buffer> buf = webrtc::I420Buffer::Create(kWidth, kHeight);
        for (int r = 0; r < kHeight; ++r) {
            for (int c = 0; c < kWidth; ++c) {
                bool in_bar = std::abs(c - bar_center_x) <= 12;
                buf->MutableDataY()[r * buf->StrideY() + c] = in_bar ? 230 : 30;
            }
        }
        std::memset(buf->MutableDataU(), 128, static_cast<size_t>(buf->StrideU()) * (kHeight / 2));
        std::memset(buf->MutableDataV(), 128, static_cast<size_t>(buf->StrideV()) * (kHeight / 2));

        ASSERT_EQ(
                encoder->Encode(webrtc::VideoFrame::Builder()
                                        .set_video_frame_buffer(buf)
                                        .set_rtp_timestamp(static_cast<uint32_t>(90000 + i * 3000))
                                        .build(),
                                &delta_req),
                WEBRTC_VIDEO_CODEC_OK);

        // Must emit 1 frame immediately without waiting for future frames (zero lookahead/B-frames)
        ASSERT_EQ(callback.Frames().size(), static_cast<size_t>(i + 1));

        const auto& encoded_img = callback.Frames().back();
        pkt->data = const_cast<uint8_t*>(encoded_img.data());
        pkt->size = static_cast<int>(encoded_img.size());
        ASSERT_GE(avcodec_send_packet(dec_ctx.get(), pkt.get()), 0);
        ASSERT_EQ(avcodec_receive_frame(dec_ctx.get(), decoded.get()), 0)
                << "Frame " << i << " was not immediately decodable (unexpected reordering delay)";

        uint8_t bar_luma = decoded->data[0][(kHeight / 2) * decoded->linesize[0] + bar_center_x];
        int bg_x = (bar_center_x + 160) % kWidth;
        uint8_t bg_luma = decoded->data[0][(kHeight / 2) * decoded->linesize[0] + bg_x];
        EXPECT_GT(bar_luma, 190) << "Expected bright bar at x=" << bar_center_x << " on frame "
                                 << i;
        EXPECT_LT(bg_luma, 70) << "Expected dark background at x=" << bg_x << " on frame " << i;
    }

    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 14. Validates multiple concurrent NvencVideoEncoder sessions encoding different display
//     resolutions in parallel (multi-display / multi-peer isolation).
TEST_F(NvencHardwareTest, ConcurrentEncoderSessionsForMultiDisplay) {
    auto encoder_720p = NvencVideoEncoder::Create(shared_loader_);
    auto encoder_480p = NvencVideoEncoder::Create(shared_loader_);
    ASSERT_NE(encoder_720p, nullptr);
    ASSERT_NE(encoder_480p, nullptr);

    CapturingEncodedImageCallback callback_720p;
    CapturingEncodedImageCallback callback_480p;
    ASSERT_EQ(encoder_720p->RegisterEncodeCompleteCallback(&callback_720p), WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(encoder_480p->RegisterEncodeCompleteCallback(&callback_480p), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoCodec codec_720p = MakeCodecSettings(1280, 720, 3000, 30);
    webrtc::VideoCodec codec_480p = MakeCodecSettings(640, 480, 1500, 30);
    ASSERT_EQ(encoder_720p->InitEncode(&codec_720p, DefaultEncoderSettings()),
              WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(encoder_480p->InitEncode(&codec_480p, DefaultEncoderSettings()),
              WEBRTC_VIDEO_CODEC_OK);

    std::vector<webrtc::VideoFrameType> delta_req = {webrtc::VideoFrameType::kVideoFrameDelta};
    for (int i = 0; i < 5; ++i) {
        ASSERT_EQ(encoder_720p->Encode(CreatePatternedI420Frame(1280, 720, i, 1000 + i * 3000),
                                       &delta_req),
                  WEBRTC_VIDEO_CODEC_OK);
        ASSERT_EQ(encoder_480p->Encode(CreatePatternedI420Frame(640, 480, i, 2000 + i * 3000),
                                       &delta_req),
                  WEBRTC_VIDEO_CODEC_OK);
    }

    ASSERT_EQ(callback_720p.Frames().size(), 5U);
    ASSERT_EQ(callback_480p.Frames().size(), 5U);
    EXPECT_EQ(callback_720p.Frames()[0]._encodedWidth, 1280U);
    EXPECT_EQ(callback_720p.Frames()[0]._encodedHeight, 720U);
    EXPECT_EQ(callback_480p.Frames()[0]._encodedWidth, 640U);
    EXPECT_EQ(callback_480p.Frames()[0]._encodedHeight, 480U);

    EXPECT_EQ(encoder_720p->Release(), WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(encoder_480p->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 15. Validates that unsupported hardware dimensions (0x0, 16x16 below NVENC H.264 min,
//     8192x8192 above NVENC H.264 4096 max) are rejected cleanly and the encoder instance
//     recovers when subsequently initialized with valid dimensions.
TEST_F(NvencHardwareTest, RejectsUnsupportedHardwareDimensionsAndRecovers) {
    auto encoder = NvencVideoEncoder::Create(shared_loader_);
    ASSERT_NE(encoder, nullptr);

    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    const std::vector<std::pair<uint16_t, uint16_t>> invalid_resolutions = {
        {0, 0},
        {16, 16},
        {8192, 8192},
    };

    for (const auto& [bad_w, bad_h] : invalid_resolutions) {
        webrtc::VideoCodec bad_codec = MakeCodecSettings(bad_w, bad_h, 2000, 30);
        EXPECT_EQ(encoder->InitEncode(&bad_codec, DefaultEncoderSettings()),
                  WEBRTC_VIDEO_CODEC_ERROR)
                << "Expected InitEncode to reject unsupported resolution " << bad_w << "x" << bad_h;
    }

    // Verify the same encoder instance cleanly recovers when initialized with valid 640x480
    // dimensions.
    webrtc::VideoCodec valid_codec = MakeCodecSettings(640, 480, 2000, 30);
    ASSERT_EQ(encoder->InitEncode(&valid_codec, DefaultEncoderSettings()), WEBRTC_VIDEO_CODEC_OK);

    std::vector<webrtc::VideoFrameType> key_req = {webrtc::VideoFrameType::kVideoFrameKey};
    ASSERT_EQ(encoder->Encode(CreatePatternedI420Frame(640, 480, 0, 90000), &key_req),
              WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(callback.Frames().size(), 1U);
    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 16. Validates that CreatePlatformVideoEncoderFactory() auto-detects the NVIDIA GPU and
//     instantiates the hardware-accelerated NvencVideoEncoder for H.264.
TEST_F(NvencHardwareTest, PlatformEncoderFactorySelectsNvencHardware) {
    auto factory = CreatePlatformVideoEncoderFactory();
    ASSERT_NE(factory, nullptr);

    webrtc::SdpVideoFormat h264_format("H264", {{"profile-level-id", "42e01f"}});
    auto encoder = factory->Create(webrtc::CreateEnvironment(), h264_format);
    ASSERT_NE(encoder, nullptr);

    webrtc::VideoEncoder::EncoderInfo info = encoder->GetEncoderInfo();
    EXPECT_TRUE(info.is_hardware_accelerated);
    EXPECT_EQ(info.implementation_name, "NVIDIA NVENC H.264");

    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoCodec codec = MakeCodecSettings(640, 360, 2000, 30);
    ASSERT_EQ(encoder->InitEncode(&codec, DefaultEncoderSettings()), WEBRTC_VIDEO_CODEC_OK);

    std::vector<webrtc::VideoFrameType> key_req = {webrtc::VideoFrameType::kVideoFrameKey};
    ASSERT_EQ(encoder->Encode(CreatePatternedI420Frame(640, 360, 0, 90000), &key_req),
              WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(callback.Frames().size(), 1U);
    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

}  // namespace
}  // namespace goldfish::videobridge
