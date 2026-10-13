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

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/strings/match.h"

#include "api/environment/environment_factory.h"
#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/nv12_buffer.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/video_encoder.h"
#include "core/linux/vaapi_loader.h"
#include "core/linux/vaapi_video_encoder.h"
#include "core/linux/vaapi_video_encoder_factory.h"
#include "goldfish/videobridge/codec_factories.h"
#include "libyuv/compare.h"
#include "libyuv/convert.h"
#include "modules/video_coding/codecs/h264/include/h264.h"
#include "modules/video_coding/include/video_error_codes.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace goldfish::videobridge {
namespace {

struct NalUnitInfo {
    uint8_t type;
    size_t offset;
    size_t size;
};

std::vector<NalUnitInfo> ParseAnnexBNalUnits(const uint8_t* data, size_t size) {
    std::vector<NalUnitInfo> nalus;
    if (!data || size < 4) return nalus;

    std::vector<size_t> start_offsets;
    for (size_t i = 0; i + 3 < size; ++i) {
        if (data[i] == 0x00 && data[i + 1] == 0x00) {
            if (data[i + 2] == 0x01) {
                start_offsets.push_back(i + 3);
                i += 2;
            } else if (i + 4 < size && data[i + 2] == 0x00 && data[i + 3] == 0x01) {
                start_offsets.push_back(i + 4);
                i += 3;
            }
        }
    }

    for (size_t idx = 0; idx < start_offsets.size(); ++idx) {
        size_t payload_start = start_offsets[idx];
        if (payload_start >= size) continue;
        size_t next_end = size;
        if (idx + 1 < start_offsets.size()) {
            next_end = start_offsets[idx + 1] - 3;
            if (next_end > 0 && data[next_end - 1] == 0x00) {
                --next_end;
            }
        }
        uint8_t nal_type = data[payload_start] & 0x1F;
        nalus.push_back({nal_type, payload_start, next_end - payload_start});
    }
    return nalus;
}

bool HasNalUnitType(const std::vector<NalUnitInfo>& nalus, uint8_t target_type) {
    for (const auto& nalu : nalus) {
        if (nalu.type == target_type) return true;
    }
    return false;
}

class CapturingEncodedImageCallback : public webrtc::EncodedImageCallback {
  public:
    struct CapturedFrame {
        std::vector<uint8_t> bitstream;
        webrtc::VideoFrameType frame_type;
        uint32_t width;
        uint32_t height;
        uint32_t rtp_timestamp;
        int64_t capture_time_ms;
        webrtc::CodecSpecificInfo codec_info;
    };

    Result OnEncodedImage(const webrtc::EncodedImage& encoded_image,
                          const webrtc::CodecSpecificInfo* codec_specific_info) override {
        CapturedFrame frame;
        if (encoded_image.data() && encoded_image.size() > 0) {
            frame.bitstream.assign(encoded_image.data(),
                                   encoded_image.data() + encoded_image.size());
        }
        frame.frame_type = encoded_image._frameType;
        frame.width = encoded_image._encodedWidth;
        frame.height = encoded_image._encodedHeight;
        frame.rtp_timestamp = encoded_image.RtpTimestamp();
        frame.capture_time_ms = encoded_image.capture_time_ms_;
        if (codec_specific_info) {
            frame.codec_info = *codec_specific_info;
        }
        frames_.push_back(std::move(frame));
        return Result(Result::OK);
    }

    void OnFrameDropped(uint32_t /*rtp_timestamp*/, int /*spatial_id*/,
                        bool /*is_end_of_temporal_unit*/) override {}

    const std::vector<CapturedFrame>& Frames() const { return frames_; }
    void Clear() { frames_.clear(); }

  private:
    std::vector<CapturedFrame> frames_;
};

std::vector<webrtc::scoped_refptr<webrtc::I420Buffer>> DecodeAnnexBFramesWithFfmpeg(
        const std::vector<CapturingEncodedImageCallback::CapturedFrame>& encoded_frames) {
    std::vector<webrtc::scoped_refptr<webrtc::I420Buffer>> decoded;
    const AVCodec* h264_decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!h264_decoder) return decoded;

    std::unique_ptr<AVCodecContext, void (*)(AVCodecContext*)> dec_ctx(
            avcodec_alloc_context3(h264_decoder),
            [](AVCodecContext* ctx) { avcodec_free_context(&ctx); });
    if (!dec_ctx || avcodec_open2(dec_ctx.get(), h264_decoder, nullptr) < 0) {
        return decoded;
    }

    std::unique_ptr<AVPacket, void (*)(AVPacket*)> pkt(av_packet_alloc(),
                                                       [](AVPacket* p) { av_packet_free(&p); });
    std::unique_ptr<AVFrame, void (*)(AVFrame*)> frame(av_frame_alloc(),
                                                       [](AVFrame* f) { av_frame_free(&f); });

    auto drain_frames = [&]() {
        while (avcodec_receive_frame(dec_ctx.get(), frame.get()) == 0) {
            auto i420 = webrtc::I420Buffer::Copy(
                    frame->width, frame->height, frame->data[0], frame->linesize[0], frame->data[1],
                    frame->linesize[1], frame->data[2], frame->linesize[2]);
            decoded.push_back(std::move(i420));
        }
    };

    for (const auto& ef : encoded_frames) {
        pkt->data = const_cast<uint8_t*>(ef.bitstream.data());
        pkt->size = static_cast<int>(ef.bitstream.size());
        if (avcodec_send_packet(dec_ctx.get(), pkt.get()) >= 0) {
            drain_frames();
        }
    }
    avcodec_send_packet(dec_ctx.get(), nullptr);
    drain_frames();
    return decoded;
}

webrtc::VideoFrame CreateGradientI420Frame(int width, int height, int frame_index,
                                           uint32_t rtp_timestamp, int64_t render_time_ms = 0) {
    webrtc::scoped_refptr<webrtc::I420Buffer> buffer = webrtc::I420Buffer::Create(width, height);
    uint8_t* y_plane = buffer->MutableDataY();
    uint8_t* u_plane = buffer->MutableDataU();
    uint8_t* v_plane = buffer->MutableDataV();

    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            y_plane[r * buffer->StrideY() + c] =
                    static_cast<uint8_t>(16 + ((c + r + frame_index * 8) % 200));
        }
    }
    for (int r = 0; r < (height + 1) / 2; ++r) {
        for (int c = 0; c < (width + 1) / 2; ++c) {
            u_plane[r * buffer->StrideU() + c] =
                    static_cast<uint8_t>(64 + ((c + frame_index) % 128));
            v_plane[r * buffer->StrideV() + c] =
                    static_cast<uint8_t>(64 + ((r + frame_index) % 128));
        }
    }

    return webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(buffer)
            .set_rtp_timestamp(rtp_timestamp)
            .set_timestamp_ms(render_time_ms)
            .set_rotation(webrtc::kVideoRotation_0)
            .build();
}

webrtc::VideoFrame CreateGradientNv12Frame(int width, int height, int frame_index,
                                           uint32_t rtp_timestamp) {
    webrtc::scoped_refptr<webrtc::NV12Buffer> buffer = webrtc::NV12Buffer::Create(width, height);
    uint8_t* y_plane = buffer->MutableDataY();
    uint8_t* uv_plane = buffer->MutableDataUV();

    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            y_plane[r * buffer->StrideY() + c] =
                    static_cast<uint8_t>(16 + ((c + r + frame_index * 8) % 200));
        }
    }
    for (int r = 0; r < (height + 1) / 2; ++r) {
        for (int c = 0; c < (width + 1) / 2; ++c) {
            uv_plane[r * buffer->StrideUV() + c * 2] =
                    static_cast<uint8_t>(64 + ((c + frame_index) % 128));
            uv_plane[r * buffer->StrideUV() + c * 2 + 1] =
                    static_cast<uint8_t>(64 + ((r + frame_index) % 128));
        }
    }

    return webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(buffer)
            .set_rtp_timestamp(rtp_timestamp)
            .set_rotation(webrtc::kVideoRotation_0)
            .build();
}

webrtc::VideoCodec MakeDefaultH264CodecSettings(uint16_t width = 1280, uint16_t height = 720,
                                                uint32_t start_bitrate_kbps = 2000,
                                                uint32_t max_framerate = 30) {
    webrtc::VideoCodec settings{};
    settings.codecType = webrtc::kVideoCodecH264;
    settings.width = width;
    settings.height = height;
    settings.startBitrate = start_bitrate_kbps;
    settings.maxBitrate = start_bitrate_kbps * 2;
    settings.minBitrate = 100;
    settings.maxFramerate = max_framerate;
    settings.numberOfSimulcastStreams = 0;
    return settings;
}

class VaapiHardwareTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto loader_or = VaapiLoader::Create();
        if (!loader_or.ok()) {
            GTEST_SKIP() << "Skipping VA-API hardware test: no Intel/AMD VA-API H.264 encoder "
                            "detected ("
                         << loader_or.status().message() << ")";
        }
        loader_ = std::move(*loader_or);
    }

    std::shared_ptr<VaapiLoader> loader_;
};

// 1. Hardware Discovery & VADisplay / Function Table Initialization
TEST_F(VaapiHardwareTest, LoaderInitializesDisplayAndFunctionTable) {
    ASSERT_NE(loader_, nullptr);
    EXPECT_NE(loader_->display(), nullptr);
    EXPECT_NE(loader_->api().vaInitialize, nullptr);
    EXPECT_NE(loader_->api().vaTerminate, nullptr);
    EXPECT_NE(loader_->api().vaCreateConfig, nullptr);
    EXPECT_NE(loader_->api().vaCreateSurfaces, nullptr);
    EXPECT_NE(loader_->api().vaCreateContext, nullptr);
    EXPECT_NE(loader_->api().vaCreateBuffer, nullptr);
    EXPECT_NE(loader_->api().vaBeginPicture, nullptr);
    EXPECT_NE(loader_->api().vaRenderPicture, nullptr);
    EXPECT_NE(loader_->api().vaEndPicture, nullptr);
    EXPECT_NE(loader_->api().vaSyncSurface, nullptr);
    EXPECT_NE(loader_->api().vaDeriveImage, nullptr);
}

// 2. Hardware Capability Probing via VaapiVideoEncoderFactory
TEST_F(VaapiHardwareTest, FactoryProbesSupportedH264FormatsOnHardware) {
    auto factory = VaapiVideoEncoderFactory::Create();
    ASSERT_NE(factory, nullptr);

    std::vector<webrtc::SdpVideoFormat> formats = factory->GetSupportedFormats();
    EXPECT_FALSE(formats.empty());

    bool found_constrained_baseline = false;
    bool found_main = false;
    bool found_high = false;
    for (const auto& fmt : formats) {
        EXPECT_EQ(fmt.name, "H264");
        auto it = fmt.parameters.find("profile-level-id");
        if (it != fmt.parameters.end()) {
            if (absl::StartsWithIgnoreCase(it->second, "42")) found_constrained_baseline = true;
            if (absl::StartsWithIgnoreCase(it->second, "4d")) found_main = true;
            if (absl::StartsWithIgnoreCase(it->second, "64")) found_high = true;
        }
    }
    EXPECT_TRUE(found_constrained_baseline);
    EXPECT_TRUE(found_main);
    EXPECT_TRUE(found_high);
}

// 3. End-to-End I420 & NV12 Encoding and Annex-B Bitstream Structure
TEST_F(VaapiHardwareTest, EncodesI420FramesAndProducesValidAnnexBBitstream) {
    auto encoder = VaapiVideoEncoder::Create(loader_, VAProfileH264ConstrainedBaseline);
    ASSERT_NE(encoder, nullptr);

    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoCodec codec_settings = MakeDefaultH264CodecSettings(1280, 720, 2000, 30);
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, settings), WEBRTC_VIDEO_CODEC_OK);

    for (int i = 0; i < 5; ++i) {
        std::vector<webrtc::VideoFrameType> types = {
            i == 0 ? webrtc::VideoFrameType::kVideoFrameKey
                   : webrtc::VideoFrameType::kVideoFrameDelta};
        webrtc::VideoFrame frame = (i % 2 == 0)
                                           ? CreateGradientI420Frame(1280, 720, i, 1000 + i * 3000)
                                           : CreateGradientNv12Frame(1280, 720, i, 1000 + i * 3000);
        ASSERT_EQ(encoder->Encode(frame, &types), WEBRTC_VIDEO_CODEC_OK);
    }

    ASSERT_EQ(callback.Frames().size(), 5U);

    // First frame must be a KeyFrame with SPS (7), PPS (8), and IDR slice (5)
    const auto& first_frame = callback.Frames()[0];
    EXPECT_EQ(first_frame.frame_type, webrtc::VideoFrameType::kVideoFrameKey);
    EXPECT_EQ(first_frame.width, 1280U);
    EXPECT_EQ(first_frame.height, 720U);
    EXPECT_EQ(first_frame.rtp_timestamp, 1000U);

    auto first_nalus =
            ParseAnnexBNalUnits(first_frame.bitstream.data(), first_frame.bitstream.size());
    EXPECT_TRUE(HasNalUnitType(first_nalus, 7)) << "Missing SPS NALU in keyframe";
    EXPECT_TRUE(HasNalUnitType(first_nalus, 8)) << "Missing PPS NALU in keyframe";
    EXPECT_TRUE(HasNalUnitType(first_nalus, 5)) << "Missing IDR slice NALU in keyframe";

    // Subsequent frames must be Delta frames with non-IDR slice (1)
    for (size_t i = 1; i < callback.Frames().size(); ++i) {
        const auto& delta = callback.Frames()[i];
        EXPECT_EQ(delta.frame_type, webrtc::VideoFrameType::kVideoFrameDelta);
        auto nalus = ParseAnnexBNalUnits(delta.bitstream.data(), delta.bitstream.size());
        EXPECT_TRUE(HasNalUnitType(nalus, 1) || HasNalUnitType(nalus, 5));
    }

    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 4. Mid-Stream Forced KeyFrame (IDR + SPS/PPS) on Demand
TEST_F(VaapiHardwareTest, ForcesMidStreamKeyFrameOnDemand) {
    auto encoder = VaapiVideoEncoder::Create(loader_, VAProfileH264ConstrainedBaseline);
    ASSERT_NE(encoder, nullptr);

    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoCodec codec_settings = MakeDefaultH264CodecSettings(640, 360, 1000, 30);
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, settings), WEBRTC_VIDEO_CODEC_OK);

    for (int i = 0; i < 6; ++i) {
        bool request_key = (i == 0 || i == 4);
        std::vector<webrtc::VideoFrameType> types = {
            request_key ? webrtc::VideoFrameType::kVideoFrameKey
                        : webrtc::VideoFrameType::kVideoFrameDelta};
        webrtc::VideoFrame frame = CreateGradientI420Frame(640, 360, i, 5000 + i * 3000);
        ASSERT_EQ(encoder->Encode(frame, &types), WEBRTC_VIDEO_CODEC_OK);
    }

    ASSERT_EQ(callback.Frames().size(), 6U);
    EXPECT_EQ(callback.Frames()[0].frame_type, webrtc::VideoFrameType::kVideoFrameKey);
    EXPECT_EQ(callback.Frames()[1].frame_type, webrtc::VideoFrameType::kVideoFrameDelta);
    EXPECT_EQ(callback.Frames()[2].frame_type, webrtc::VideoFrameType::kVideoFrameDelta);
    EXPECT_EQ(callback.Frames()[3].frame_type, webrtc::VideoFrameType::kVideoFrameDelta);
    EXPECT_EQ(callback.Frames()[4].frame_type, webrtc::VideoFrameType::kVideoFrameKey);

    auto mid_idr_nalus = ParseAnnexBNalUnits(callback.Frames()[4].bitstream.data(),
                                             callback.Frames()[4].bitstream.size());
    EXPECT_TRUE(HasNalUnitType(mid_idr_nalus, 7)) << "Mid-stream IDR must repeat SPS";
    EXPECT_TRUE(HasNalUnitType(mid_idr_nalus, 8)) << "Mid-stream IDR must repeat PPS";
    EXPECT_TRUE(HasNalUnitType(mid_idr_nalus, 5)) << "Mid-stream IDR must contain IDR slice";
}

// 5. Dynamic Bitrate & Framerate Reconfiguration via SetRates()
TEST_F(VaapiHardwareTest, HandlesDynamicBitrateAndFramerateAdjustments) {
    auto encoder = VaapiVideoEncoder::Create(loader_, VAProfileH264Main);
    ASSERT_NE(encoder, nullptr);

    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoCodec codec_settings = MakeDefaultH264CodecSettings(1280, 720, 500, 30);
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, settings), WEBRTC_VIDEO_CODEC_OK);

    // Encode 10 frames at low bitrate (500 kbps)
    webrtc::VideoEncoder::RateControlParameters low_rates;
    low_rates.bitrate.SetBitrate(0, 0, 500000);
    low_rates.framerate_fps = 30.0;
    encoder->SetRates(low_rates);

    size_t low_bitrate_bytes = 0;
    for (int i = 0; i < 10; ++i) {
        std::vector<webrtc::VideoFrameType> types = {webrtc::VideoFrameType::kVideoFrameDelta};
        ASSERT_EQ(encoder->Encode(CreateGradientI420Frame(1280, 720, i, 1000 + i * 3000), &types),
                  WEBRTC_VIDEO_CODEC_OK);
        low_bitrate_bytes += callback.Frames().back().bitstream.size();
    }

    // Reconfigure to high bitrate (8 Mbps)
    webrtc::VideoEncoder::RateControlParameters high_rates;
    high_rates.bitrate.SetBitrate(0, 0, 8000000);
    high_rates.framerate_fps = 60.0;
    encoder->SetRates(high_rates);

    size_t high_bitrate_bytes = 0;
    for (int i = 10; i < 20; ++i) {
        std::vector<webrtc::VideoFrameType> types = {
            i == 10 ? webrtc::VideoFrameType::kVideoFrameKey
                    : webrtc::VideoFrameType::kVideoFrameDelta};
        ASSERT_EQ(encoder->Encode(CreateGradientI420Frame(1280, 720, i, 1000 + i * 3000), &types),
                  WEBRTC_VIDEO_CODEC_OK);
        high_bitrate_bytes += callback.Frames().back().bitstream.size();
    }

    EXPECT_GT(low_bitrate_bytes, 0U);
    EXPECT_GT(high_bitrate_bytes, 0U);
}

// 6. Dynamic Resolution Change & Surface Reallocation
TEST_F(VaapiHardwareTest, HandlesDynamicResolutionChangeAndBufferReallocation) {
    auto encoder = VaapiVideoEncoder::Create(loader_, VAProfileH264ConstrainedBaseline);
    ASSERT_NE(encoder, nullptr);

    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);

    // Start at 640x360
    webrtc::VideoCodec settings_360p = MakeDefaultH264CodecSettings(640, 360, 1000, 30);
    ASSERT_EQ(encoder->InitEncode(&settings_360p, settings), WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(encoder->Encode(CreateGradientI420Frame(640, 360, 0, 1000), nullptr),
              WEBRTC_VIDEO_CODEC_OK);

    // Mismatched 1080p frame without InitEncode must return ERR_PARAMETER
    EXPECT_EQ(encoder->Encode(CreateGradientI420Frame(1920, 1080, 1, 2000), nullptr),
              WEBRTC_VIDEO_CODEC_ERR_PARAMETER);

    // Re-initialize for 1920x1080
    webrtc::VideoCodec settings_1080p = MakeDefaultH264CodecSettings(1920, 1080, 4000, 30);
    ASSERT_EQ(encoder->InitEncode(&settings_1080p, settings), WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(encoder->Encode(CreateGradientI420Frame(1920, 1080, 2, 3000), nullptr),
              WEBRTC_VIDEO_CODEC_OK);

    ASSERT_EQ(callback.Frames().size(), 2U);
    EXPECT_EQ(callback.Frames()[0].width, 640U);
    EXPECT_EQ(callback.Frames()[0].height, 360U);
    EXPECT_EQ(callback.Frames()[1].width, 1920U);
    EXPECT_EQ(callback.Frames()[1].height, 1080U);
}

// 7. Non-16-Aligned Resolution & Stride/Cropping Validation (854x480 FWVGA)
TEST_F(VaapiHardwareTest, HandlesNon16AlignedResolutionWithCorrectStride) {
    auto encoder = VaapiVideoEncoder::Create(loader_, VAProfileH264ConstrainedBaseline);
    ASSERT_NE(encoder, nullptr);

    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoCodec codec_settings = MakeDefaultH264CodecSettings(854, 480, 1500, 30);
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, settings), WEBRTC_VIDEO_CODEC_OK);

    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(encoder->Encode(CreateGradientI420Frame(854, 480, i, 1000 + i * 3000), nullptr),
                  WEBRTC_VIDEO_CODEC_OK);
    }

    ASSERT_EQ(callback.Frames().size(), 3U);
    EXPECT_EQ(callback.Frames()[0].width, 854U);
    EXPECT_EQ(callback.Frames()[0].height, 480U);
}

// 8. Hardware Encode -> Software OpenH264 Decode Round-Trip & PSNR Visual Fidelity
TEST_F(VaapiHardwareTest, DecodesHardwareBitstreamAndVerifiesVisualFidelity) {
    auto encoder = VaapiVideoEncoder::Create(loader_, VAProfileH264ConstrainedBaseline);
    ASSERT_NE(encoder, nullptr);

    CapturingEncodedImageCallback enc_callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&enc_callback), WEBRTC_VIDEO_CODEC_OK);

    constexpr int kWidth = 640;
    constexpr int kHeight = 360;
    webrtc::VideoCodec codec_settings = MakeDefaultH264CodecSettings(kWidth, kHeight, 3000, 30);
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, settings), WEBRTC_VIDEO_CODEC_OK);

    std::vector<webrtc::VideoFrame> source_frames;
    for (int i = 0; i < 4; ++i) {
        webrtc::VideoFrame src = CreateGradientI420Frame(kWidth, kHeight, i, 1000 + i * 3000);
        source_frames.push_back(src);
        ASSERT_EQ(encoder->Encode(src, nullptr), WEBRTC_VIDEO_CODEC_OK);
    }

    auto decoded_frames = DecodeAnnexBFramesWithFfmpeg(enc_callback.Frames());
    ASSERT_EQ(decoded_frames.size(), 4U);
    for (size_t i = 0; i < decoded_frames.size(); ++i) {
        const auto& dec_i420 = decoded_frames[i];
        EXPECT_EQ(dec_i420->width(), kWidth);
        EXPECT_EQ(dec_i420->height(), kHeight);

        auto src_i420 = source_frames[i].video_frame_buffer()->ToI420();
        double psnr = libyuv::I420Psnr(src_i420->DataY(), src_i420->StrideY(), src_i420->DataU(),
                                       src_i420->StrideU(), src_i420->DataV(), src_i420->StrideV(),
                                       dec_i420->DataY(), dec_i420->StrideY(), dec_i420->DataU(),
                                       dec_i420->StrideU(), dec_i420->DataV(), dec_i420->StrideV(),
                                       kWidth, kHeight);
        EXPECT_GE(psnr, 30.0) << "PSNR too low on frame " << i;
    }
}

// 9. Spatial Quadrant Color & Luma Gradient Preservation (Catches UV Swap / Pitch Skew)
TEST_F(VaapiHardwareTest, PreservesSpatialQuadrantColorsAndLumaGradient) {
    constexpr int kWidth = 640;
    constexpr int kHeight = 480;
    webrtc::scoped_refptr<webrtc::I420Buffer> i420 = webrtc::I420Buffer::Create(kWidth, kHeight);

    // Top-Left: Red (Y=82, U=90, V=240), Top-Right: Green (Y=145, U=54, V=34)
    // Bottom-Left: Blue (Y=41, U=240, V=110), Bottom-Right: Horizontal Luma Ramp (U=128, V=128)
    for (int r = 0; r < kHeight; ++r) {
        for (int c = 0; c < kWidth; ++c) {
            uint8_t y_val = 0;
            if (r < kHeight / 2 && c < kWidth / 2) {
                y_val = 82;
            } else if (r < kHeight / 2 && c >= kWidth / 2) {
                y_val = 145;
            } else if (r >= kHeight / 2 && c < kWidth / 2) {
                y_val = 41;
            } else {
                y_val = static_cast<uint8_t>(32 + ((c - kWidth / 2) * 180) /
                                                          std::max(1, (kWidth / 2 - 1)));
            }
            i420->MutableDataY()[r * i420->StrideY() + c] = y_val;
        }
    }
    for (int r = 0; r < kHeight / 2; ++r) {
        for (int c = 0; c < kWidth / 2; ++c) {
            uint8_t u_val = 128;
            uint8_t v_val = 128;
            if (r < kHeight / 4 && c < kWidth / 4) {
                u_val = 90;
                v_val = 240;
            } else if (r < kHeight / 4 && c >= kWidth / 4) {
                u_val = 54;
                v_val = 34;
            } else if (r >= kHeight / 4 && c < kWidth / 4) {
                u_val = 240;
                v_val = 110;
            }
            i420->MutableDataU()[r * i420->StrideU() + c] = u_val;
            i420->MutableDataV()[r * i420->StrideV() + c] = v_val;
        }
    }

    auto encoder = VaapiVideoEncoder::Create(loader_, VAProfileH264ConstrainedBaseline);
    CapturingEncodedImageCallback enc_callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&enc_callback), WEBRTC_VIDEO_CODEC_OK);
    webrtc::VideoCodec codec_settings = MakeDefaultH264CodecSettings(kWidth, kHeight, 4000, 30);
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, settings), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoFrame src_frame = webrtc::VideoFrame::Builder()
                                           .set_video_frame_buffer(i420)
                                           .set_rtp_timestamp(1000)
                                           .build();
    ASSERT_EQ(encoder->Encode(src_frame, nullptr), WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(enc_callback.Frames().size(), 1U);

    auto decoded_frames = DecodeAnnexBFramesWithFfmpeg(enc_callback.Frames());
    ASSERT_EQ(decoded_frames.size(), 1U);
    auto dec_i420 = decoded_frames[0];
    auto sample_yuv = [&](int x, int y, int& out_y, int& out_u, int& out_v) {
        out_y = dec_i420->DataY()[y * dec_i420->StrideY() + x];
        out_u = dec_i420->DataU()[(y / 2) * dec_i420->StrideU() + (x / 2)];
        out_v = dec_i420->DataV()[(y / 2) * dec_i420->StrideV() + (x / 2)];
    };

    int y = 0, u = 0, v = 0;
    sample_yuv(kWidth / 4, kHeight / 4, y, u, v);
    EXPECT_NEAR(y, 82, 15);
    EXPECT_NEAR(u, 90, 20);
    EXPECT_NEAR(v, 240, 20);

    sample_yuv((kWidth * 3) / 4, kHeight / 4, y, u, v);
    EXPECT_NEAR(y, 145, 15);
    EXPECT_NEAR(u, 54, 20);
    EXPECT_NEAR(v, 34, 20);

    sample_yuv(kWidth / 4, (kHeight * 3) / 4, y, u, v);
    EXPECT_NEAR(y, 41, 15);
    EXPECT_NEAR(u, 240, 20);
    EXPECT_NEAR(v, 110, 20);

    int y_left = 0, y_mid = 0, y_right = 0;
    sample_yuv(kWidth / 2 + 20, (kHeight * 3) / 4, y_left, u, v);
    sample_yuv((kWidth * 3) / 4, (kHeight * 3) / 4, y_mid, u, v);
    sample_yuv(kWidth - 20, (kHeight * 3) / 4, y_right, u, v);
    EXPECT_LT(y_left, y_mid);
    EXPECT_LT(y_mid, y_right);
}

// 10. Negotiated H.264 Profile IDC Verification in SPS NALU (Baseline=66, Main=77, High=100)
TEST_F(VaapiHardwareTest, EnforcesRequestedH264ProfileIdcInBitstreamSps) {
    struct ProfileTestCase {
        const char* profile_level_id;
        uint8_t expected_profile_idc;
    };
    const ProfileTestCase k_cases[] = {
        {"42e01f", 66},   // Constrained Baseline
        {"4d001f", 77},   // Main
        {"640c1f", 100},  // High
    };

    for (const auto& tc : k_cases) {
        webrtc::SdpVideoFormat format("H264", {{"profile-level-id", tc.profile_level_id}});
        auto encoder = VaapiVideoEncoder::Create(loader_, format);
        ASSERT_NE(encoder, nullptr);

        CapturingEncodedImageCallback callback;
        ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

        webrtc::VideoCodec codec_settings = MakeDefaultH264CodecSettings(640, 360, 1500, 30);
        webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1,
                                                1000000);
        ASSERT_EQ(encoder->InitEncode(&codec_settings, settings), WEBRTC_VIDEO_CODEC_OK);
        ASSERT_EQ(encoder->Encode(CreateGradientI420Frame(640, 360, 0, 1000), nullptr),
                  WEBRTC_VIDEO_CODEC_OK);
        ASSERT_EQ(callback.Frames().size(), 1U);

        const auto& bs = callback.Frames()[0].bitstream;
        auto nalus = ParseAnnexBNalUnits(bs.data(), bs.size());
        bool found_sps = false;
        for (const auto& nalu : nalus) {
            if (nalu.type == 7 && nalu.offset + 1 < bs.size()) {
                found_sps = true;
                EXPECT_EQ(bs[nalu.offset + 1], tc.expected_profile_idc)
                        << "Mismatched profile_idc for SDP profile-level-id="
                        << tc.profile_level_id;
            }
        }
        EXPECT_TRUE(found_sps);
    }
}

// 11. Portrait & Foldable Device Aspect Ratios (720x1280, 1080x2400, 1840x2208)
TEST_F(VaapiHardwareTest, EncodesPortraitAndFoldableAspectRatios) {
    const std::pair<uint16_t, uint16_t> k_resolutions[] = {
        {720, 1280},   // 9:16 Portrait Phone
        {1080, 2400},  // 9:20 Modern Tall Phone
        {1840, 2208},  // Unfolded Pixel Fold (non-16-aligned width)
    };

    for (const auto& [w, h] : k_resolutions) {
        auto encoder = VaapiVideoEncoder::Create(loader_, VAProfileH264ConstrainedBaseline);
        CapturingEncodedImageCallback callback;
        ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

        webrtc::VideoCodec codec_settings = MakeDefaultH264CodecSettings(w, h, 3000, 30);
        webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1,
                                                1000000);
        ASSERT_EQ(encoder->InitEncode(&codec_settings, settings), WEBRTC_VIDEO_CODEC_OK);
        ASSERT_EQ(encoder->Encode(CreateGradientI420Frame(w, h, 0, 1000), nullptr),
                  WEBRTC_VIDEO_CODEC_OK);
        ASSERT_EQ(callback.Frames().size(), 1U);
        EXPECT_EQ(callback.Frames()[0].width, w);
        EXPECT_EQ(callback.Frames()[0].height, h);
    }
}

// 12. Cross-Thread VADisplay & Context Usage (InitEncode on Main Thread, Encode on Worker Thread)
TEST_F(VaapiHardwareTest, SupportsEncodeOnWorkerThreadSeparateFromInitThread) {
    auto encoder = VaapiVideoEncoder::Create(loader_, VAProfileH264ConstrainedBaseline);
    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoCodec codec_settings = MakeDefaultH264CodecSettings(1280, 720, 2000, 30);
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, settings), WEBRTC_VIDEO_CODEC_OK);

    std::thread worker([&]() {
        for (int i = 0; i < 4; ++i) {
            EXPECT_EQ(encoder->Encode(CreateGradientI420Frame(1280, 720, i, 1000 + i * 3000),
                                      nullptr),
                      WEBRTC_VIDEO_CODEC_OK);
        }
    });
    worker.join();

    EXPECT_EQ(callback.Frames().size(), 4U);
    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 13. Ultra-Low Latency Zero-B-Frame Verification (Strict Monotonic Timestamps & Immediate Output)
TEST_F(VaapiHardwareTest, EmitsZeroBFramesForUltraLowLatency) {
    auto encoder = VaapiVideoEncoder::Create(loader_, VAProfileH264Main);
    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoCodec codec_settings = MakeDefaultH264CodecSettings(640, 360, 1500, 30);
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, settings), WEBRTC_VIDEO_CODEC_OK);

    for (int i = 0; i < 8; ++i) {
        size_t before = callback.Frames().size();
        uint32_t rtp_ts = 10000 + i * 3000;
        int64_t capture_ms = 100 + i * 33;
        ASSERT_EQ(
                encoder->Encode(CreateGradientI420Frame(640, 360, i, rtp_ts, capture_ms), nullptr),
                WEBRTC_VIDEO_CODEC_OK);
        // Each Encode() must immediately emit 1 frame without B-frame lookahead buffering
        ASSERT_EQ(callback.Frames().size(), before + 1);
        EXPECT_EQ(callback.Frames().back().rtp_timestamp, rtp_ts);
        EXPECT_EQ(callback.Frames().back().capture_time_ms, capture_ms);
    }
}

// 14. Concurrent Multi-Display VA-API Sessions (1080p Primary + 720p Secondary Display)
TEST_F(VaapiHardwareTest, SupportsConcurrentEncoderSessionsForMultiDisplay) {
    auto enc1 = VaapiVideoEncoder::Create(loader_, VAProfileH264ConstrainedBaseline);
    auto enc2 = VaapiVideoEncoder::Create(loader_, VAProfileH264Main);
    ASSERT_NE(enc1, nullptr);
    ASSERT_NE(enc2, nullptr);

    CapturingEncodedImageCallback cb1;
    CapturingEncodedImageCallback cb2;
    ASSERT_EQ(enc1->RegisterEncodeCompleteCallback(&cb1), WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(enc2->RegisterEncodeCompleteCallback(&cb2), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);
    webrtc::VideoCodec codec1 = MakeDefaultH264CodecSettings(1920, 1080, 3000, 30);
    webrtc::VideoCodec codec2 = MakeDefaultH264CodecSettings(1280, 720, 1500, 30);
    ASSERT_EQ(enc1->InitEncode(&codec1, settings), WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(enc2->InitEncode(&codec2, settings), WEBRTC_VIDEO_CODEC_OK);

    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(enc1->Encode(CreateGradientI420Frame(1920, 1080, i, 1000 + i * 3000), nullptr),
                  WEBRTC_VIDEO_CODEC_OK);
        ASSERT_EQ(enc2->Encode(CreateGradientI420Frame(1280, 720, i, 2000 + i * 3000), nullptr),
                  WEBRTC_VIDEO_CODEC_OK);
    }

    EXPECT_EQ(cb1.Frames().size(), 3U);
    EXPECT_EQ(cb2.Frames().size(), 3U);
}

// 15. Graceful Error Recovery on Invalid Dimensions (0x0 and 16384x16384)
TEST_F(VaapiHardwareTest, FailsGracefullyOnInvalidDimensionsAndRecovers) {
    auto encoder = VaapiVideoEncoder::Create(loader_, VAProfileH264ConstrainedBaseline);
    CapturingEncodedImageCallback callback;
    ASSERT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);
    webrtc::VideoEncoder::Settings settings(webrtc::VideoEncoder::Capabilities(false), 1, 1000000);

    webrtc::VideoCodec zero_codec = MakeDefaultH264CodecSettings(0, 0, 1000, 30);
    EXPECT_NE(encoder->InitEncode(&zero_codec, settings), WEBRTC_VIDEO_CODEC_OK);

    webrtc::VideoCodec huge_codec = MakeDefaultH264CodecSettings(16384, 16384, 1000, 30);
    EXPECT_NE(encoder->InitEncode(&huge_codec, settings), WEBRTC_VIDEO_CODEC_OK);

    // Must cleanly recover when initialized with valid 720p dimensions
    webrtc::VideoCodec valid_codec = MakeDefaultH264CodecSettings(1280, 720, 2000, 30);
    ASSERT_EQ(encoder->InitEncode(&valid_codec, settings), WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(encoder->Encode(CreateGradientI420Frame(1280, 720, 0, 1000), nullptr),
              WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(callback.Frames().size(), 1U);
}

// 16. Platform Codec Factory End-to-End Integration (CreatePlatformVideoEncoderFactory)
TEST_F(VaapiHardwareTest, PlatformCodecFactorySelectsHardwareVaapiOnSupportedHost) {
    webrtc::Environment env = webrtc::CreateEnvironment();

    auto vaapi_factory = VaapiVideoEncoderFactory::Create();
    ASSERT_NE(vaapi_factory, nullptr);
    auto vaapi_encoder = vaapi_factory->Create(env, webrtc::SdpVideoFormat("H264"));
    ASSERT_NE(vaapi_encoder, nullptr);
    auto vaapi_info = vaapi_encoder->GetEncoderInfo();
    EXPECT_TRUE(vaapi_info.is_hardware_accelerated);
    EXPECT_NE(vaapi_info.implementation_name.find("VA-API"), std::string::npos);

    auto platform_factory = CreatePlatformVideoEncoderFactory();
    ASSERT_NE(platform_factory, nullptr);

    auto formats = platform_factory->GetSupportedFormats();
    ASSERT_FALSE(formats.empty());

    auto encoder = platform_factory->Create(env, webrtc::SdpVideoFormat("H264"));
    ASSERT_NE(encoder, nullptr);

    auto info = encoder->GetEncoderInfo();
    EXPECT_TRUE(info.is_hardware_accelerated);
    EXPECT_TRUE(info.implementation_name.find("VA-API") != std::string::npos ||
                info.implementation_name.find("NVENC") != std::string::npos)
            << "Unexpected implementation_name: " << info.implementation_name;
}

}  // namespace
}  // namespace goldfish::videobridge
