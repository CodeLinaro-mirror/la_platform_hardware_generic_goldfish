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

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
}

#include "goldfish/videobridge/codec_factories.h"
#include "libyuv/compare.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/environment/environment_factory.h"
#include "api/scoped_refptr.h"
#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/nv12_buffer.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#pragma clang diagnostic pop

namespace goldfish::videobridge {
namespace {

constexpr const char* kTestSuiteVersion = "1.0-20260923";

std::string CFStringToStdString(CFStringRef str) {
    if (!str) {
        return "";
    }
    CFIndex length = CFStringGetLength(str);
    CFIndex max_size = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string buffer(max_size, '\0');
    if (CFStringGetCString(str, buffer.data(), max_size, kCFStringEncodingUTF8)) {
        buffer.resize(std::strlen(buffer.c_str()));
        return buffer;
    }
    return "";
}

// Helper to query all available VideoToolbox H.264 encoders and check if hardware
// acceleration is supported on this macOS host.
std::vector<std::string> DiscoverHardwareVideoToolboxEncoders() {
    std::vector<std::string> encoder_names;
    CFArrayRef encoder_list = nullptr;
    OSStatus status = VTCopyVideoEncoderList(nullptr, &encoder_list);
    if (status == noErr && encoder_list != nullptr) {
        CFIndex count = CFArrayGetCount(encoder_list);
        for (CFIndex i = 0; i < count; ++i) {
            auto dict = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(encoder_list, i));
            if (!dict) continue;
            auto codec_type = static_cast<CFNumberRef>(
                    CFDictionaryGetValue(dict, kVTVideoEncoderList_CodecType));
            int32_t ctype = 0;
            if (codec_type && CFNumberGetValue(codec_type, kCFNumberSInt32Type, &ctype) &&
                ctype == kCMVideoCodecType_H264) {
                auto is_hw = static_cast<CFBooleanRef>(
                        CFDictionaryGetValue(dict, kVTVideoEncoderList_IsHardwareAccelerated));
                if (is_hw && CFBooleanGetValue(is_hw)) {
                    auto display_name = static_cast<CFStringRef>(
                            CFDictionaryGetValue(dict, kVTVideoEncoderList_DisplayName));
                    auto encoder_name = static_cast<CFStringRef>(
                            CFDictionaryGetValue(dict, kVTVideoEncoderList_EncoderName));
                    std::string name =
                            CFStringToStdString(display_name ? display_name : encoder_name);
                    encoder_names.push_back(name.empty() ? "Apple H.264 VideoToolbox Encoder"
                                                         : name);
                }
            }
        }
        CFRelease(encoder_list);
    }

    // Also verify a hardware-accelerated 1280x720 VTCompressionSession can be created.
    VTCompressionSessionRef session = nullptr;
    const void* keys[] = {kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder};
    const void* values[] = {kCFBooleanTrue};
    CFDictionaryRef encoder_spec =
            CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
                               &kCFTypeDictionaryValueCallBacks);
    status = VTCompressionSessionCreate(kCFAllocatorDefault, 1280, 720, kCMVideoCodecType_H264,
                                        encoder_spec, nullptr, nullptr, nullptr, nullptr, &session);
    if (encoder_spec) {
        CFRelease(encoder_spec);
    }
    if (status == noErr && session != nullptr) {
        CFBooleanRef using_hw = nullptr;
        OSStatus prop_status = VTSessionCopyProperty(
                session, kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder,
                kCFAllocatorDefault, &using_hw);
        bool is_session_hw =
                (prop_status == noErr && using_hw != nullptr && CFBooleanGetValue(using_hw));
        if (using_hw) {
            CFRelease(using_hw);
        }
        if (is_session_hw) {
            if (encoder_names.empty()) {
                encoder_names.push_back("Apple H.264 VideoToolbox Session");
            }
        } else {
            encoder_names.clear();
        }
        VTCompressionSessionInvalidate(session);
        CFRelease(session);
    } else {
        encoder_names.clear();
    }

    return encoder_names;
}

bool HasHardwareVideoToolboxH264Support() {
    return !DiscoverHardwareVideoToolboxEncoders().empty();
}

// Thread-safe helper to capture encoded frames from VideoToolbox's callback queue.
class CapturingEncodedImageCallback : public webrtc::EncodedImageCallback {
  public:
    struct CapturedFrame {
        std::vector<uint8_t> bitstream;
        webrtc::VideoFrameType frame_type;
        uint32_t width;
        uint32_t height;
        uint32_t rtp_timestamp;
        webrtc::CodecSpecificInfo codec_specific;
    };

    Result OnEncodedImage(const webrtc::EncodedImage& encoded_image,
                          const webrtc::CodecSpecificInfo* codec_specific_info) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            CapturedFrame frame;
            if (encoded_image.data() && encoded_image.size() > 0) {
                frame.bitstream.assign(encoded_image.data(),
                                       encoded_image.data() + encoded_image.size());
            }
            frame.frame_type = encoded_image._frameType;
            frame.width = encoded_image._encodedWidth;
            frame.height = encoded_image._encodedHeight;
            frame.rtp_timestamp = encoded_image.RtpTimestamp();
            if (codec_specific_info) {
                frame.codec_specific = *codec_specific_info;
            }
            frames_.push_back(std::move(frame));
        }
        cv_.notify_all();
        return Result(Result::OK);
    }

    void OnFrameDropped(uint32_t /*rtp_timestamp*/, int /*spatial_id*/,
                        bool /*is_end_of_temporal_unit*/) override {}

    bool WaitForFrames(size_t expected_count, int timeout_ms = 3000) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                            [this, expected_count]() { return frames_.size() >= expected_count; });
    }

    std::vector<CapturedFrame> GetFrames() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return frames_;
    }

    size_t FrameCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return frames_.size();
    }

  private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<CapturedFrame> frames_;
};

// Parses Annex-B start codes (00 00 01 or 00 00 00 01) and extracts NAL unit types.
std::vector<uint8_t> ExtractH264NalUnitTypes(const std::vector<uint8_t>& bitstream) {
    std::vector<uint8_t> nal_types;
    size_t i = 0;
    while (i + 3 < bitstream.size()) {
        if (bitstream[i] == 0x00 && bitstream[i + 1] == 0x00) {
            if (bitstream[i + 2] == 0x01) {
                if (i + 3 < bitstream.size()) {
                    nal_types.push_back(bitstream[i + 3] & 0x1F);
                }
                i += 4;
                continue;
            } else if (i + 4 < bitstream.size() && bitstream[i + 2] == 0x00 &&
                       bitstream[i + 3] == 0x01) {
                nal_types.push_back(bitstream[i + 4] & 0x1F);
                i += 5;
                continue;
            }
        }
        ++i;
    }
    return nal_types;
}

// Extracts the `profile_idc` byte (first payload byte after NAL header) from the first
// SPS NAL unit (type 7) in an Annex-B bitstream. Returns 0 if no SPS NAL unit is found.
uint8_t ExtractSpsProfileIdc(const std::vector<uint8_t>& bitstream) {
    size_t i = 0;
    while (i + 4 < bitstream.size()) {
        if (bitstream[i] == 0x00 && bitstream[i + 1] == 0x00) {
            size_t nal_header_idx = 0;
            if (bitstream[i + 2] == 0x01) {
                nal_header_idx = i + 3;
            } else if (i + 5 < bitstream.size() && bitstream[i + 2] == 0x00 &&
                       bitstream[i + 3] == 0x01) {
                nal_header_idx = i + 4;
            }
            if (nal_header_idx > 0 && nal_header_idx + 1 < bitstream.size()) {
                uint8_t nal_type = bitstream[nal_header_idx] & 0x1F;
                if (nal_type == 7) {
                    return bitstream[nal_header_idx + 1];
                }
                i = nal_header_idx + 1;
                continue;
            }
        }
        ++i;
    }
    return 0;
}

// Extracts the full NAL header bytes (`nal_ref_idc` + `nal_unit_type`) for all VCL
// NAL units (types 1..5) in an Annex-B bitstream.
std::vector<uint8_t> ExtractVclNalHeaders(const std::vector<uint8_t>& bitstream) {
    std::vector<uint8_t> headers;
    size_t i = 0;
    while (i + 3 < bitstream.size()) {
        if (bitstream[i] == 0x00 && bitstream[i + 1] == 0x00) {
            size_t nal_header_idx = 0;
            if (bitstream[i + 2] == 0x01) {
                nal_header_idx = i + 3;
            } else if (i + 4 < bitstream.size() && bitstream[i + 2] == 0x00 &&
                       bitstream[i + 3] == 0x01) {
                nal_header_idx = i + 4;
            }
            if (nal_header_idx > 0 && nal_header_idx < bitstream.size()) {
                uint8_t header = bitstream[nal_header_idx];
                uint8_t nal_type = header & 0x1F;
                if (nal_type >= 1 && nal_type <= 5) {
                    headers.push_back(header);
                }
                i = nal_header_idx + 1;
                continue;
            }
        }
        ++i;
    }
    return headers;
}

// Generates a deterministic synthetic I420 frame with a moving color gradient.
webrtc::scoped_refptr<webrtc::I420Buffer> CreateSyntheticI420Frame(int width, int height,
                                                                   int frame_index) {
    auto buffer = webrtc::I420Buffer::Create(width, height);
    uint8_t* y_plane = buffer->MutableDataY();
    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            y_plane[r * buffer->StrideY() + c] =
                    static_cast<uint8_t>((r + c + frame_index * 11) % 220 + 16);
        }
    }
    uint8_t* u_plane = buffer->MutableDataU();
    uint8_t* v_plane = buffer->MutableDataV();
    for (int r = 0; r < (height + 1) / 2; ++r) {
        for (int c = 0; c < (width + 1) / 2; ++c) {
            u_plane[r * buffer->StrideU() + c] =
                    static_cast<uint8_t>((r * 2 + frame_index * 5) % 200 + 28);
            v_plane[r * buffer->StrideV() + c] =
                    static_cast<uint8_t>((c * 2 + frame_index * 7) % 200 + 28);
        }
    }
    return buffer;
}

// Generates a 4-quadrant test chart in I420 with distinct YUV colors in each quadrant
// and a horizontal luma gradient in the bottom-right quadrant.
webrtc::scoped_refptr<webrtc::I420Buffer> CreateQuadrantTestChartI420(int width, int height) {
    auto buffer = webrtc::I420Buffer::Create(width, height);
    const int half_w = width / 2;
    const int half_h = height / 2;

    uint8_t* y_plane = buffer->MutableDataY();
    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            uint8_t y_val = 128;
            if (r < half_h && c < half_w) {
                y_val = 210;  // Top-Left: Bright
            } else if (r < half_h && c >= half_w) {
                y_val = 40;  // Top-Right: Dark
            } else if (r >= half_h && c < half_w) {
                y_val = 128;  // Bottom-Left: Mid-Luma
            } else {
                // Bottom-Right: Horizontal Luma ramp from 32 to 220
                int rel_c = c - half_w;
                y_val = static_cast<uint8_t>(32 + (rel_c * 188) / std::max(1, width - half_w - 1));
            }
            y_plane[r * buffer->StrideY() + c] = y_val;
        }
    }

    uint8_t* u_plane = buffer->MutableDataU();
    uint8_t* v_plane = buffer->MutableDataV();
    const int uv_h = (height + 1) / 2;
    const int uv_w = (width + 1) / 2;
    const int half_uv_h = uv_h / 2;
    const int half_uv_w = uv_w / 2;

    for (int r = 0; r < uv_h; ++r) {
        for (int c = 0; c < uv_w; ++c) {
            uint8_t u_val = 128;
            uint8_t v_val = 128;
            if (r < half_uv_h && c < half_uv_w) {
                // Top-Left: High U, Low V (Blue-ish)
                u_val = 200;
                v_val = 60;
            } else if (r < half_uv_h && c >= half_uv_w) {
                // Top-Right: Low U, High V (Red-ish)
                u_val = 60;
                v_val = 200;
            } else if (r >= half_uv_h && c < half_uv_w) {
                // Bottom-Left: Low U, Low V (Green-ish)
                u_val = 50;
                v_val = 50;
            } else {
                // Bottom-Right: Neutral Grayscale (U=128, V=128)
                u_val = 128;
                v_val = 128;
            }
            u_plane[r * buffer->StrideU() + c] = u_val;
            v_plane[r * buffer->StrideV() + c] = v_val;
        }
    }
    return buffer;
}

class TrackingNV12Buffer : public webrtc::NV12Buffer {
  public:
    static webrtc::scoped_refptr<TrackingNV12Buffer> Create(int width, int height) {
        return webrtc::make_ref_counted<TrackingNV12Buffer>(width, height);
    }

    webrtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override {
        ++to_i420_calls_;
        return webrtc::NV12Buffer::ToI420();
    }

    int to_i420_calls() const { return to_i420_calls_; }

  protected:
    TrackingNV12Buffer(int width, int height) : webrtc::NV12Buffer(width, height) {}

  private:
    int to_i420_calls_ = 0;
};

// Generates a deterministic synthetic NV12 frame.
webrtc::scoped_refptr<TrackingNV12Buffer> CreateSyntheticNV12Frame(int width, int height,
                                                                   int frame_index) {
    auto buffer = TrackingNV12Buffer::Create(width, height);
    uint8_t* y_plane = buffer->MutableDataY();
    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            y_plane[r * buffer->StrideY() + c] =
                    static_cast<uint8_t>((r + c + frame_index * 13) % 220 + 16);
        }
    }
    uint8_t* uv_plane = buffer->MutableDataUV();
    for (int r = 0; r < (height + 1) / 2; ++r) {
        for (int c = 0; c < (width + 1) / 2; ++c) {
            uv_plane[r * buffer->StrideUV() + c * 2] =
                    static_cast<uint8_t>((r * 3 + frame_index * 5) % 200 + 28);
            uv_plane[r * buffer->StrideUV() + c * 2 + 1] =
                    static_cast<uint8_t>((c * 3 + frame_index * 7) % 200 + 28);
        }
    }
    return buffer;
}

// Helper class to decode an Annex-B H.264 bitstream into I420 frames using FFmpeg.
class H264BitstreamDecoder {
  public:
    H264BitstreamDecoder() {
        const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        if (codec) {
            ctx_ = avcodec_alloc_context3(codec);
            if (ctx_) {
                if (avcodec_open2(ctx_, codec, nullptr) < 0) {
                    avcodec_free_context(&ctx_);
                }
            }
        }
        frame_ = av_frame_alloc();
        pkt_ = av_packet_alloc();
    }

    ~H264BitstreamDecoder() {
        if (pkt_) av_packet_free(&pkt_);
        if (frame_) av_frame_free(&frame_);
        if (ctx_) avcodec_free_context(&ctx_);
    }

    bool IsValid() const { return ctx_ != nullptr && frame_ != nullptr && pkt_ != nullptr; }

    std::vector<webrtc::scoped_refptr<webrtc::I420Buffer>> DecodeAnnexB(
            const std::vector<uint8_t>& bitstream) {
        std::vector<webrtc::scoped_refptr<webrtc::I420Buffer>> out;
        if (!IsValid() || bitstream.empty()) {
            return out;
        }

        pkt_->data = const_cast<uint8_t*>(bitstream.data());
        pkt_->size = static_cast<int>(bitstream.size());
        if (avcodec_send_packet(ctx_, pkt_) < 0) {
            return out;
        }
        DrainFrames(out);
        return out;
    }

    std::vector<webrtc::scoped_refptr<webrtc::I420Buffer>> Flush() {
        std::vector<webrtc::scoped_refptr<webrtc::I420Buffer>> out;
        if (!IsValid()) {
            return out;
        }
        avcodec_send_packet(ctx_, nullptr);
        DrainFrames(out);
        return out;
    }

  private:
    void DrainFrames(std::vector<webrtc::scoped_refptr<webrtc::I420Buffer>>& out) {
        while (avcodec_receive_frame(ctx_, frame_) == 0) {
            auto i420 = webrtc::I420Buffer::Copy(
                    frame_->width, frame_->height, frame_->data[0], frame_->linesize[0],
                    frame_->data[1], frame_->linesize[1], frame_->data[2], frame_->linesize[2]);
            out.push_back(i420);
        }
    }

    AVCodecContext* ctx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* pkt_ = nullptr;
};

class VideoToolboxHardwareTest : public ::testing::Test {
  protected:
    webrtc::Environment env_ = webrtc::CreateEnvironment();
    webrtc::VideoEncoder::Settings default_settings_{webrtc::VideoEncoder::Capabilities(false), 1,
                                                     1200};

    webrtc::VideoCodec CreateDefaultH264CodecSettings(uint16_t width = 640, uint16_t height = 480,
                                                      uint32_t start_bitrate_kbps = 2000,
                                                      uint32_t max_framerate = 30) {
        webrtc::VideoCodec codec;
        codec.codecType = webrtc::kVideoCodecH264;
        codec.width = width;
        codec.height = height;
        codec.startBitrate = start_bitrate_kbps;
        codec.maxBitrate = start_bitrate_kbps * 2;
        codec.minBitrate = 100;
        codec.maxFramerate = max_framerate;
        codec.numberOfSimulcastStreams = 0;
        codec.mode = webrtc::VideoCodecMode::kRealtimeVideo;
        return codec;
    }

    std::unique_ptr<webrtc::VideoEncoder> CreateHardwareEncoder(
            const std::string& profile_level_id = "640c34") {
        auto factory = CreatePlatformVideoEncoderFactory();
        if (!factory) {
            return nullptr;
        }
        webrtc::SdpVideoFormat format("H264", {{"profile-level-id", profile_level_id},
                                               {"packetization-mode", "1"},
                                               {"level-asymmetry-allowed", "1"}});
        return factory->Create(env_, format);
    }
};

// 1. Hardware VideoToolbox Probing & Session Creation
TEST_F(VideoToolboxHardwareTest, HardwareEncoderProbingAndSessionCreation) {
    std::printf("[VideoToolbox Hardware Test] Version: %s\n", kTestSuiteVersion);
    std::fflush(stdout);
    auto encoders = DiscoverHardwareVideoToolboxEncoders();
    if (encoders.empty()) {
        GTEST_SKIP() << "No hardware VideoToolbox H.264 encoder available on this macOS host.";
    }

    std::printf("[VideoToolbox Hardware Test] Discovered %zu hardware H.264 encoder(s):\n",
                encoders.size());
    for (const auto& name : encoders) {
        std::printf("  * %s\n", name.c_str());
    }
    EXPECT_FALSE(encoders.empty());
}

// 2. Factory Format Probing
TEST_F(VideoToolboxHardwareTest, FactoryProbesSupportedH264FormatsOnHardware) {
    if (!HasHardwareVideoToolboxH264Support()) {
        GTEST_SKIP() << "Skipping: hardware H.264 VideoToolbox not supported on this host.";
    }

    auto factory = CreatePlatformVideoEncoderFactory();
    ASSERT_NE(factory, nullptr);
    auto formats = factory->GetSupportedFormats();
    EXPECT_FALSE(formats.empty());

    bool has_constrained_baseline = false;
    bool has_high = false;
    for (const auto& fmt : formats) {
        if (fmt.name == "H264") {
            auto it = fmt.parameters.find("profile-level-id");
            if (it != fmt.parameters.end()) {
                if (it->second == "42e034") has_constrained_baseline = true;
                if (it->second == "640c34") has_high = true;
            }
            EXPECT_EQ(fmt.parameters.at("packetization-mode"), "1");
            EXPECT_EQ(fmt.parameters.at("level-asymmetry-allowed"), "1");
        }
    }
    EXPECT_TRUE(has_constrained_baseline);
    EXPECT_TRUE(has_high);
}

// 3. End-to-End I420 Hardware Encoding & Annex-B Bitstream Verification
TEST_F(VideoToolboxHardwareTest, EncodesI420FramesAndProducesValidAnnexBBitstream) {
    if (!HasHardwareVideoToolboxH264Support()) {
        GTEST_SKIP() << "Skipping: hardware H.264 VideoToolbox not supported on this host.";
    }

    CapturingEncodedImageCallback callback;
    auto encoder = CreateHardwareEncoder("640c34");
    ASSERT_NE(encoder, nullptr);
    EXPECT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    auto codec_settings = CreateDefaultH264CodecSettings(640, 480, 2000, 30);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, default_settings_), WEBRTC_VIDEO_CODEC_OK);

    auto info = encoder->GetEncoderInfo();
    EXPECT_TRUE(info.is_hardware_accelerated);

    // Encode 10 frames (first frame must be an IDR keyframe with SPS/PPS, followed by P-frames)
    constexpr int kNumFrames = 10;
    for (int i = 0; i < kNumFrames; ++i) {
        auto i420 = CreateSyntheticI420Frame(640, 480, i);
        webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                           .set_video_frame_buffer(i420)
                                           .set_rtp_timestamp(90000 + i * 3000)
                                           .set_timestamp_us(i * 33333)
                                           .build();
        std::vector<webrtc::VideoFrameType> frame_types = {
            i == 0 ? webrtc::VideoFrameType::kVideoFrameKey
                   : webrtc::VideoFrameType::kVideoFrameDelta};
        EXPECT_EQ(encoder->Encode(frame, &frame_types), WEBRTC_VIDEO_CODEC_OK);
    }

    EXPECT_TRUE(callback.WaitForFrames(kNumFrames));
    const auto& frames = callback.GetFrames();
    ASSERT_EQ(frames.size(), static_cast<size_t>(kNumFrames));

    // Verify Frame 0 is a keyframe containing SPS (7), PPS (8), and IDR slice (5)
    EXPECT_EQ(frames[0].frame_type, webrtc::VideoFrameType::kVideoFrameKey);
    EXPECT_EQ(frames[0].width, 640u);
    EXPECT_EQ(frames[0].height, 480u);
    EXPECT_EQ(frames[0].rtp_timestamp, 90000u);
    EXPECT_EQ(frames[0].codec_specific.codecType, webrtc::kVideoCodecH264);

    auto first_frame_nal_types = ExtractH264NalUnitTypes(frames[0].bitstream);
    EXPECT_NE(std::find(first_frame_nal_types.begin(), first_frame_nal_types.end(), 7),
              first_frame_nal_types.end())
            << "Keyframe must contain SPS NAL unit (type 7)";
    EXPECT_NE(std::find(first_frame_nal_types.begin(), first_frame_nal_types.end(), 8),
              first_frame_nal_types.end())
            << "Keyframe must contain PPS NAL unit (type 8)";
    EXPECT_NE(std::find(first_frame_nal_types.begin(), first_frame_nal_types.end(), 5),
              first_frame_nal_types.end())
            << "Keyframe must contain IDR slice NAL unit (type 5)";

    // Verify subsequent frames are delta frames containing non-IDR slices (type 1)
    for (size_t i = 1; i < frames.size(); ++i) {
        EXPECT_EQ(frames[i].frame_type, webrtc::VideoFrameType::kVideoFrameDelta);
        EXPECT_GT(frames[i].bitstream.size(), 0u);
        auto nal_types = ExtractH264NalUnitTypes(frames[i].bitstream);
        EXPECT_NE(std::find(nal_types.begin(), nal_types.end(), 1), nal_types.end())
                << "Delta frame " << i << " should contain non-IDR slice NAL unit (type 1)";
    }

    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 4. End-to-End NV12 Native Buffer Encoding
TEST_F(VideoToolboxHardwareTest, EncodesNV12FramesAndProducesValidAnnexBBitstream) {
    if (!HasHardwareVideoToolboxH264Support()) {
        GTEST_SKIP() << "Skipping: hardware H.264 VideoToolbox not supported on this host.";
    }

    CapturingEncodedImageCallback callback;
    auto encoder = CreateHardwareEncoder("640c34");
    ASSERT_NE(encoder, nullptr);
    EXPECT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    auto codec_settings = CreateDefaultH264CodecSettings(640, 480, 2000, 30);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, default_settings_), WEBRTC_VIDEO_CODEC_OK);

    for (int i = 0; i < 5; ++i) {
        auto nv12 = CreateSyntheticNV12Frame(640, 480, i);
        webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                           .set_video_frame_buffer(nv12)
                                           .set_rtp_timestamp(180000 + i * 3000)
                                           .set_timestamp_us(i * 33333)
                                           .build();
        std::vector<webrtc::VideoFrameType> frame_types = {
            i == 0 ? webrtc::VideoFrameType::kVideoFrameKey
                   : webrtc::VideoFrameType::kVideoFrameDelta};
        EXPECT_EQ(encoder->Encode(frame, &frame_types), WEBRTC_VIDEO_CODEC_OK);
        EXPECT_EQ(nv12->to_i420_calls(), 0)
                << "NV12 frame should be copied directly into a CVPixelBuffer without converting "
                   "to I420 first.";
    }

    EXPECT_TRUE(callback.WaitForFrames(5));
    const auto& frames = callback.GetFrames();
    ASSERT_EQ(frames.size(), 5u);
    EXPECT_EQ(frames[0].frame_type, webrtc::VideoFrameType::kVideoFrameKey);
    auto nal_types = ExtractH264NalUnitTypes(frames[0].bitstream);
    EXPECT_NE(std::find(nal_types.begin(), nal_types.end(), 5), nal_types.end());

    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 5. On-Demand Keyframe Request Mid-Stream
TEST_F(VideoToolboxHardwareTest, ForcesMidStreamKeyFrameOnDemand) {
    if (!HasHardwareVideoToolboxH264Support()) {
        GTEST_SKIP() << "Skipping: hardware H.264 VideoToolbox not supported on this host.";
    }

    CapturingEncodedImageCallback callback;
    auto encoder = CreateHardwareEncoder("640c34");
    ASSERT_NE(encoder, nullptr);
    EXPECT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    auto codec_settings = CreateDefaultH264CodecSettings(640, 480, 2000, 30);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, default_settings_), WEBRTC_VIDEO_CODEC_OK);

    for (int i = 0; i < 6; ++i) {
        auto i420 = CreateSyntheticI420Frame(640, 480, i);
        webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                           .set_video_frame_buffer(i420)
                                           .set_rtp_timestamp(270000 + i * 3000)
                                           .set_timestamp_us(i * 33333)
                                           .build();
        // Force a keyframe at index 0 and index 3
        std::vector<webrtc::VideoFrameType> frame_types = {
            (i == 0 || i == 3) ? webrtc::VideoFrameType::kVideoFrameKey
                               : webrtc::VideoFrameType::kVideoFrameDelta};
        EXPECT_EQ(encoder->Encode(frame, &frame_types), WEBRTC_VIDEO_CODEC_OK);
    }

    EXPECT_TRUE(callback.WaitForFrames(6));
    const auto& frames = callback.GetFrames();
    ASSERT_EQ(frames.size(), 6u);
    EXPECT_EQ(frames[0].frame_type, webrtc::VideoFrameType::kVideoFrameKey);
    EXPECT_EQ(frames[1].frame_type, webrtc::VideoFrameType::kVideoFrameDelta);
    EXPECT_EQ(frames[2].frame_type, webrtc::VideoFrameType::kVideoFrameDelta);
    EXPECT_EQ(frames[3].frame_type, webrtc::VideoFrameType::kVideoFrameKey);

    auto forced_idr_nals = ExtractH264NalUnitTypes(frames[3].bitstream);
    EXPECT_NE(std::find(forced_idr_nals.begin(), forced_idr_nals.end(), 7), forced_idr_nals.end());
    EXPECT_NE(std::find(forced_idr_nals.begin(), forced_idr_nals.end(), 8), forced_idr_nals.end());
    EXPECT_NE(std::find(forced_idr_nals.begin(), forced_idr_nals.end(), 5), forced_idr_nals.end());

    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 6. Dynamic Rate Control (`SetRates`)
TEST_F(VideoToolboxHardwareTest, HandlesDynamicBitrateAndFramerateAdjustments) {
    if (!HasHardwareVideoToolboxH264Support()) {
        GTEST_SKIP() << "Skipping: hardware H.264 VideoToolbox not supported on this host.";
    }

    CapturingEncodedImageCallback callback;
    auto encoder = CreateHardwareEncoder("640c34");
    ASSERT_NE(encoder, nullptr);
    EXPECT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    auto codec_settings = CreateDefaultH264CodecSettings(640, 480, 500, 30);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, default_settings_), WEBRTC_VIDEO_CODEC_OK);

    for (int i = 0; i < 4; ++i) {
        if (i == 2) {
            webrtc::VideoBitrateAllocation allocation;
            allocation.SetBitrate(0, 0, 4000 * 1000);  // Increase to 4 Mbps at 60 fps
            encoder->SetRates(webrtc::VideoEncoder::RateControlParameters(allocation, 60.0));
        }
        auto i420 = CreateSyntheticI420Frame(640, 480, i);
        webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                           .set_video_frame_buffer(i420)
                                           .set_rtp_timestamp(360000 + i * 3000)
                                           .set_timestamp_us(i * 33333)
                                           .build();
        std::vector<webrtc::VideoFrameType> frame_types = {
            webrtc::VideoFrameType::kVideoFrameDelta};
        EXPECT_EQ(encoder->Encode(frame, &frame_types), WEBRTC_VIDEO_CODEC_OK);
    }

    EXPECT_TRUE(callback.WaitForFrames(4));
    EXPECT_EQ(callback.FrameCount(), 4u);
    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 7. Dynamic Resolution Change Mid-Stream
TEST_F(VideoToolboxHardwareTest, HandlesDynamicResolutionChangeAndBufferReallocation) {
    if (!HasHardwareVideoToolboxH264Support()) {
        GTEST_SKIP() << "Skipping: hardware H.264 VideoToolbox not supported on this host.";
    }

    CapturingEncodedImageCallback callback;
    auto encoder = CreateHardwareEncoder("640c34");
    ASSERT_NE(encoder, nullptr);
    EXPECT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    auto codec_settings = CreateDefaultH264CodecSettings(640, 480, 2000, 30);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, default_settings_), WEBRTC_VIDEO_CODEC_OK);

    // Encode 2 frames at 640x480
    for (int i = 0; i < 2; ++i) {
        auto i420 = CreateSyntheticI420Frame(640, 480, i);
        webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                           .set_video_frame_buffer(i420)
                                           .set_rtp_timestamp(450000 + i * 3000)
                                           .set_timestamp_us(i * 33333)
                                           .build();
        std::vector<webrtc::VideoFrameType> frame_types = {
            webrtc::VideoFrameType::kVideoFrameDelta};
        EXPECT_EQ(encoder->Encode(frame, &frame_types), WEBRTC_VIDEO_CODEC_OK);
    }

    EXPECT_TRUE(callback.WaitForFrames(2));

    // Switch resolution dynamically to 1280x720 without explicitly calling InitEncode
    for (int i = 2; i < 4; ++i) {
        auto i420 = CreateSyntheticI420Frame(1280, 720, i);
        webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                           .set_video_frame_buffer(i420)
                                           .set_rtp_timestamp(450000 + i * 3000)
                                           .set_timestamp_us(i * 33333)
                                           .build();
        std::vector<webrtc::VideoFrameType> frame_types = {
            webrtc::VideoFrameType::kVideoFrameDelta};
        EXPECT_EQ(encoder->Encode(frame, &frame_types), WEBRTC_VIDEO_CODEC_OK);
    }

    EXPECT_TRUE(callback.WaitForFrames(4));
    const auto& frames = callback.GetFrames();
    ASSERT_EQ(frames.size(), 4u);
    EXPECT_EQ(frames[0].width, 640u);
    EXPECT_EQ(frames[0].height, 480u);
    EXPECT_EQ(frames[2].width, 1280u);
    EXPECT_EQ(frames[2].height, 720u);
    // Resolution change must automatically trigger a new IDR keyframe with new SPS/PPS
    EXPECT_EQ(frames[2].frame_type, webrtc::VideoFrameType::kVideoFrameKey);

    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 8. Non-16-Aligned Dimensions & Cropping
TEST_F(VideoToolboxHardwareTest, HandlesNon16AlignedResolutionWithCorrectStride) {
    if (!HasHardwareVideoToolboxH264Support()) {
        GTEST_SKIP() << "Skipping: hardware H.264 VideoToolbox not supported on this host.";
    }

    // 660x490 is not a multiple of 16 (660 = 41*16 + 4, 490 = 30*16 + 10)
    CapturingEncodedImageCallback callback;
    auto encoder = CreateHardwareEncoder("640c34");
    ASSERT_NE(encoder, nullptr);
    EXPECT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    auto codec_settings = CreateDefaultH264CodecSettings(660, 490, 2000, 30);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, default_settings_), WEBRTC_VIDEO_CODEC_OK);

    auto i420 = CreateSyntheticI420Frame(660, 490, 0);
    webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                       .set_video_frame_buffer(i420)
                                       .set_rtp_timestamp(540000)
                                       .set_timestamp_us(0)
                                       .build();
    std::vector<webrtc::VideoFrameType> frame_types = {webrtc::VideoFrameType::kVideoFrameKey};
    EXPECT_EQ(encoder->Encode(frame, &frame_types), WEBRTC_VIDEO_CODEC_OK);

    EXPECT_TRUE(callback.WaitForFrames(1));
    const auto& frames = callback.GetFrames();
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].width, 660u);
    EXPECT_EQ(frames[0].height, 490u);

    H264BitstreamDecoder decoder;
    ASSERT_TRUE(decoder.IsValid());
    auto decoded = decoder.DecodeAnnexB(frames[0].bitstream);
    auto flushed = decoder.Flush();
    decoded.insert(decoded.end(), flushed.begin(), flushed.end());
    ASSERT_FALSE(decoded.empty());
    EXPECT_EQ(decoded[0]->width(), 660);
    EXPECT_EQ(decoded[0]->height(), 490);

    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 9. Round-Trip Decode & Visual Quality (PSNR) Verification
TEST_F(VideoToolboxHardwareTest, DecodesHardwareBitstreamAndVerifiesVisualFidelity) {
    if (!HasHardwareVideoToolboxH264Support()) {
        GTEST_SKIP() << "Skipping: hardware H.264 VideoToolbox not supported on this host.";
    }

    constexpr int kWidth = 640;
    constexpr int kHeight = 480;
    constexpr int kNumFrames = 8;

    CapturingEncodedImageCallback callback;
    auto encoder = CreateHardwareEncoder("640c34");
    ASSERT_NE(encoder, nullptr);
    EXPECT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    // Use a generous 4 Mbps target bitrate so reconstructed frames have high PSNR (> 30 dB).
    auto codec_settings = CreateDefaultH264CodecSettings(kWidth, kHeight, 4000, 30);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, default_settings_), WEBRTC_VIDEO_CODEC_OK);

    std::vector<webrtc::scoped_refptr<webrtc::I420Buffer>> source_frames;
    for (int i = 0; i < kNumFrames; ++i) {
        auto i420 = CreateSyntheticI420Frame(kWidth, kHeight, i);
        source_frames.push_back(i420);
        webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                           .set_video_frame_buffer(i420)
                                           .set_rtp_timestamp(600000 + i * 3000)
                                           .set_timestamp_us(i * 33333)
                                           .build();
        std::vector<webrtc::VideoFrameType> frame_types = {
            i == 0 ? webrtc::VideoFrameType::kVideoFrameKey
                   : webrtc::VideoFrameType::kVideoFrameDelta};
        ASSERT_EQ(encoder->Encode(frame, &frame_types), WEBRTC_VIDEO_CODEC_OK);
    }

    EXPECT_TRUE(callback.WaitForFrames(kNumFrames));
    const auto& encoded_frames = callback.GetFrames();
    ASSERT_EQ(encoded_frames.size(), static_cast<size_t>(kNumFrames));

    H264BitstreamDecoder decoder;
    ASSERT_TRUE(decoder.IsValid());

    std::vector<webrtc::scoped_refptr<webrtc::I420Buffer>> decoded_frames;
    for (const auto& ef : encoded_frames) {
        auto out = decoder.DecodeAnnexB(ef.bitstream);
        decoded_frames.insert(decoded_frames.end(), out.begin(), out.end());
    }
    auto flushed = decoder.Flush();
    decoded_frames.insert(decoded_frames.end(), flushed.begin(), flushed.end());

    ASSERT_EQ(decoded_frames.size(), source_frames.size());

    double min_psnr = 100.0;
    for (size_t i = 0; i < decoded_frames.size(); ++i) {
        const auto& src = source_frames[i];
        const auto& dec = decoded_frames[i];
        ASSERT_EQ(dec->width(), kWidth);
        ASSERT_EQ(dec->height(), kHeight);

        double psnr = libyuv::I420Psnr(src->DataY(), src->StrideY(), src->DataU(), src->StrideU(),
                                       src->DataV(), src->StrideV(), dec->DataY(), dec->StrideY(),
                                       dec->DataU(), dec->StrideU(), dec->DataV(), dec->StrideV(),
                                       kWidth, kHeight);
        min_psnr = std::min(min_psnr, psnr);
        EXPECT_GE(psnr, 30.0) << "Decoded frame " << i
                              << " has unexpectedly low visual fidelity (PSNR = " << psnr
                              << " dB).";
    }
    std::printf("[VideoToolbox Hardware Test] Decoded %zu frames; minimum I420 PSNR = %.2f dB\n",
                decoded_frames.size(), min_psnr);

    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 10. Spatial Color Quadrant & Luma Gradient Preservation
TEST_F(VideoToolboxHardwareTest, PreservesSpatialQuadrantColorsAndLumaGradient) {
    if (!HasHardwareVideoToolboxH264Support()) {
        GTEST_SKIP() << "Skipping: hardware H.264 VideoToolbox not supported on this host.";
    }

    constexpr int kWidth = 640;
    constexpr int kHeight = 480;

    CapturingEncodedImageCallback callback;
    auto encoder = CreateHardwareEncoder("640c34");
    ASSERT_NE(encoder, nullptr);
    EXPECT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    auto codec_settings = CreateDefaultH264CodecSettings(kWidth, kHeight, 4000, 30);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, default_settings_), WEBRTC_VIDEO_CODEC_OK);

    auto chart = CreateQuadrantTestChartI420(kWidth, kHeight);
    webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                       .set_video_frame_buffer(chart)
                                       .set_rtp_timestamp(650000)
                                       .set_timestamp_us(0)
                                       .build();
    std::vector<webrtc::VideoFrameType> frame_types = {webrtc::VideoFrameType::kVideoFrameKey};
    ASSERT_EQ(encoder->Encode(frame, &frame_types), WEBRTC_VIDEO_CODEC_OK);

    EXPECT_TRUE(callback.WaitForFrames(1));
    const auto& frames = callback.GetFrames();
    ASSERT_EQ(frames.size(), 1u);

    H264BitstreamDecoder decoder;
    ASSERT_TRUE(decoder.IsValid());
    auto decoded = decoder.DecodeAnnexB(frames[0].bitstream);
    auto flushed = decoder.Flush();
    decoded.insert(decoded.end(), flushed.begin(), flushed.end());
    ASSERT_FALSE(decoded.empty());

    const auto& dec = decoded[0];
    ASSERT_EQ(dec->width(), kWidth);
    ASSERT_EQ(dec->height(), kHeight);

    auto sample_y = [&](int x, int y) -> int { return dec->DataY()[y * dec->StrideY() + x]; };
    auto sample_u = [&](int x, int y) -> int {
        return dec->DataU()[(y / 2) * dec->StrideU() + (x / 2)];
    };
    auto sample_v = [&](int x, int y) -> int {
        return dec->DataV()[(y / 2) * dec->StrideV() + (x / 2)];
    };

    // Top-Left center (160, 120): expected Y=210, U=200, V=60
    EXPECT_NEAR(sample_y(160, 120), 210, 10);
    EXPECT_NEAR(sample_u(160, 120), 200, 12);
    EXPECT_NEAR(sample_v(160, 120), 60, 12);

    // Top-Right center (480, 120): expected Y=40, U=60, V=200 (detects U/V chroma swap!)
    EXPECT_NEAR(sample_y(480, 120), 40, 10);
    EXPECT_NEAR(sample_u(480, 120), 60, 12);
    EXPECT_NEAR(sample_v(480, 120), 200, 12);

    // Bottom-Left center (160, 360): expected Y=128, U=50, V=50
    EXPECT_NEAR(sample_y(160, 360), 128, 10);
    EXPECT_NEAR(sample_u(160, 360), 50, 12);
    EXPECT_NEAR(sample_v(160, 360), 50, 12);

    // Bottom-Right horizontal luma gradient at y=360: monotonically increasing from x=360 to x=600
    int y_left = sample_y(360, 360);
    int y_mid = sample_y(480, 360);
    int y_right = sample_y(600, 360);
    EXPECT_LT(y_left, y_mid);
    EXPECT_LT(y_mid, y_right);
    EXPECT_NEAR(sample_u(480, 360), 128, 10);
    EXPECT_NEAR(sample_v(480, 360), 128, 10);

    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 11. H.264 Profile Negotiation & SPS `profile_idc` Enforcement
TEST_F(VideoToolboxHardwareTest, EnforcesRequestedH264ProfileIdcInBitstreamSps) {
    if (!HasHardwareVideoToolboxH264Support()) {
        GTEST_SKIP() << "Skipping: hardware H.264 VideoToolbox not supported on this host.";
    }

    struct ProfileCase {
        std::string profile_level_id;
        uint8_t expected_profile_idc;
    };
    const std::vector<ProfileCase> test_cases = {
        {"42e034", 66},   // Constrained Baseline -> profile_idc 66
        {"640c34", 100},  // Constrained High -> profile_idc 100
    };

    for (const auto& tc : test_cases) {
        CapturingEncodedImageCallback callback;
        auto encoder = CreateHardwareEncoder(tc.profile_level_id);
        ASSERT_NE(encoder, nullptr);
        EXPECT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

        auto codec_settings = CreateDefaultH264CodecSettings(640, 480, 2000, 30);
        ASSERT_EQ(encoder->InitEncode(&codec_settings, default_settings_), WEBRTC_VIDEO_CODEC_OK);

        auto i420 = CreateSyntheticI420Frame(640, 480, 0);
        webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                           .set_video_frame_buffer(i420)
                                           .set_rtp_timestamp(700000)
                                           .set_timestamp_us(0)
                                           .build();
        std::vector<webrtc::VideoFrameType> frame_types = {webrtc::VideoFrameType::kVideoFrameKey};
        ASSERT_EQ(encoder->Encode(frame, &frame_types), WEBRTC_VIDEO_CODEC_OK);

        EXPECT_TRUE(callback.WaitForFrames(1));
        const auto& frames = callback.GetFrames();
        ASSERT_EQ(frames.size(), 1u);
        uint8_t actual_profile_idc = ExtractSpsProfileIdc(frames[0].bitstream);
        EXPECT_EQ(actual_profile_idc, tc.expected_profile_idc)
                << "Mismatch in SPS profile_idc for profile-level-id=" << tc.profile_level_id;

        EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
    }
}

// 12. Portrait Phone & Foldable Display Aspect Ratios
TEST_F(VideoToolboxHardwareTest, EncodesPortraitAndFoldableAspectRatios) {
    if (!HasHardwareVideoToolboxH264Support()) {
        GTEST_SKIP() << "Skipping: hardware H.264 VideoToolbox not supported on this host.";
    }

    const std::vector<std::pair<uint16_t, uint16_t>> aspect_ratios = {
        {480, 1080},  // Tall portrait smartphone display (9:20)
        {880, 920},   // Unfolded inner foldable display (~1:1)
    };

    for (const auto& [w, h] : aspect_ratios) {
        CapturingEncodedImageCallback callback;
        auto encoder = CreateHardwareEncoder("640c34");
        ASSERT_NE(encoder, nullptr);
        EXPECT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

        auto codec_settings = CreateDefaultH264CodecSettings(w, h, 3000, 30);
        ASSERT_EQ(encoder->InitEncode(&codec_settings, default_settings_), WEBRTC_VIDEO_CODEC_OK);

        auto i420 = CreateSyntheticI420Frame(w, h, 0);
        webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                           .set_video_frame_buffer(i420)
                                           .set_rtp_timestamp(750000)
                                           .set_timestamp_us(0)
                                           .build();
        std::vector<webrtc::VideoFrameType> frame_types = {webrtc::VideoFrameType::kVideoFrameKey};
        ASSERT_EQ(encoder->Encode(frame, &frame_types), WEBRTC_VIDEO_CODEC_OK);

        EXPECT_TRUE(callback.WaitForFrames(1));
        const auto& frames = callback.GetFrames();
        ASSERT_EQ(frames.size(), 1u);
        EXPECT_EQ(frames[0].width, w);
        EXPECT_EQ(frames[0].height, h);

        H264BitstreamDecoder decoder;
        ASSERT_TRUE(decoder.IsValid());
        auto decoded = decoder.DecodeAnnexB(frames[0].bitstream);
        auto flushed = decoder.Flush();
        decoded.insert(decoded.end(), flushed.begin(), flushed.end());
        ASSERT_FALSE(decoded.empty());
        EXPECT_EQ(decoded[0]->width(), w);
        EXPECT_EQ(decoded[0]->height(), h);

        EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
    }
}

// 13. Cross-Thread Initialization vs. Worker-Thread Encoding
TEST_F(VideoToolboxHardwareTest, SupportsEncodeOnWorkerThreadSeparateFromInitThread) {
    if (!HasHardwareVideoToolboxH264Support()) {
        GTEST_SKIP() << "Skipping: hardware H.264 VideoToolbox not supported on this host.";
    }

    CapturingEncodedImageCallback callback;
    auto encoder = CreateHardwareEncoder("640c34");
    ASSERT_NE(encoder, nullptr);
    EXPECT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    // Initialize on the main test thread
    auto codec_settings = CreateDefaultH264CodecSettings(640, 480, 2000, 30);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, default_settings_), WEBRTC_VIDEO_CODEC_OK);

    // Perform Encode() and SetRates() on a separate worker thread (mirroring WebRTC's
    // EncoderQueue)
    std::thread worker([&]() {
        for (int i = 0; i < 6; ++i) {
            if (i == 3) {
                webrtc::VideoBitrateAllocation allocation;
                allocation.SetBitrate(0, 0, 3000 * 1000);
                encoder->SetRates(webrtc::VideoEncoder::RateControlParameters(allocation, 60.0));
            }
            auto i420 = CreateSyntheticI420Frame(640, 480, i);
            webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                               .set_video_frame_buffer(i420)
                                               .set_rtp_timestamp(800000 + i * 3000)
                                               .set_timestamp_us(i * 33333)
                                               .build();
            std::vector<webrtc::VideoFrameType> frame_types = {
                i == 0 ? webrtc::VideoFrameType::kVideoFrameKey
                       : webrtc::VideoFrameType::kVideoFrameDelta};
            EXPECT_EQ(encoder->Encode(frame, &frame_types), WEBRTC_VIDEO_CODEC_OK);
        }
    });
    worker.join();

    EXPECT_TRUE(callback.WaitForFrames(6));
    EXPECT_EQ(callback.FrameCount(), 6u);
    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 14. Zero-Latency / No B-Frame Emission Verification
TEST_F(VideoToolboxHardwareTest, EmitsZeroBFramesForUltraLowLatency) {
    if (!HasHardwareVideoToolboxH264Support()) {
        GTEST_SKIP() << "Skipping: hardware H.264 VideoToolbox not supported on this host.";
    }

    CapturingEncodedImageCallback callback;
    auto encoder = CreateHardwareEncoder("640c34");
    ASSERT_NE(encoder, nullptr);
    EXPECT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    auto codec_settings = CreateDefaultH264CodecSettings(640, 480, 2000, 30);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, default_settings_), WEBRTC_VIDEO_CODEC_OK);

    // Verify zero B-frame reordering: every VCL NAL unit in the output stream
    // must be a reference picture (`nal_ref_idc != 0`, i.e., I-frame or P-frame, never a
    // non-reference B-frame).
    constexpr int kNumFrames = 12;
    for (int i = 0; i < kNumFrames; ++i) {
        auto i420 = CreateSyntheticI420Frame(640, 480, i);
        webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                           .set_video_frame_buffer(i420)
                                           .set_rtp_timestamp(850000 + i * 3000)
                                           .set_timestamp_us(i * 33333)
                                           .build();
        std::vector<webrtc::VideoFrameType> frame_types = {
            i == 0 ? webrtc::VideoFrameType::kVideoFrameKey
                   : webrtc::VideoFrameType::kVideoFrameDelta};
        ASSERT_EQ(encoder->Encode(frame, &frame_types), WEBRTC_VIDEO_CODEC_OK);
    }

    EXPECT_TRUE(callback.WaitForFrames(kNumFrames));
    const auto& frames = callback.GetFrames();
    ASSERT_EQ(frames.size(), static_cast<size_t>(kNumFrames));
    for (int i = 0; i < kNumFrames; ++i) {
        auto vcl_headers = ExtractVclNalHeaders(frames[i].bitstream);
        ASSERT_FALSE(vcl_headers.empty());
        for (uint8_t header : vcl_headers) {
            uint8_t nal_ref_idc = (header >> 5) & 0x03;
            EXPECT_NE(nal_ref_idc, 0u) << "Frame " << i << " emitted a non-reference B-slice!";
        }
    }

    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 15. Concurrent Multi-Display Hardware Encoder Instances
TEST_F(VideoToolboxHardwareTest, SupportsConcurrentEncoderSessionsForMultiDisplay) {
    if (!HasHardwareVideoToolboxH264Support()) {
        GTEST_SKIP() << "Skipping: hardware H.264 VideoToolbox not supported on this host.";
    }

    CapturingEncodedImageCallback callback_primary;
    CapturingEncodedImageCallback callback_secondary;
    auto encoder_primary = CreateHardwareEncoder("640c34");
    auto encoder_secondary = CreateHardwareEncoder("640c34");
    ASSERT_NE(encoder_primary, nullptr);
    ASSERT_NE(encoder_secondary, nullptr);

    EXPECT_EQ(encoder_primary->RegisterEncodeCompleteCallback(&callback_primary),
              WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(encoder_secondary->RegisterEncodeCompleteCallback(&callback_secondary),
              WEBRTC_VIDEO_CODEC_OK);

    auto settings_primary = CreateDefaultH264CodecSettings(1280, 720, 3000, 30);
    auto settings_secondary = CreateDefaultH264CodecSettings(640, 480, 1500, 30);

    ASSERT_EQ(encoder_primary->InitEncode(&settings_primary, default_settings_),
              WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(encoder_secondary->InitEncode(&settings_secondary, default_settings_),
              WEBRTC_VIDEO_CODEC_OK);

    for (int i = 0; i < 5; ++i) {
        auto frame1_buf = CreateSyntheticI420Frame(1280, 720, i);
        auto frame2_buf = CreateSyntheticI420Frame(640, 480, i + 100);

        webrtc::VideoFrame frame1 = webrtc::VideoFrame::Builder()
                                            .set_video_frame_buffer(frame1_buf)
                                            .set_rtp_timestamp(900000 + i * 3000)
                                            .set_timestamp_us(i * 33333)
                                            .build();
        webrtc::VideoFrame frame2 = webrtc::VideoFrame::Builder()
                                            .set_video_frame_buffer(frame2_buf)
                                            .set_rtp_timestamp(900000 + i * 3000)
                                            .set_timestamp_us(i * 33333)
                                            .build();

        std::vector<webrtc::VideoFrameType> types = {
            i == 0 ? webrtc::VideoFrameType::kVideoFrameKey
                   : webrtc::VideoFrameType::kVideoFrameDelta};
        EXPECT_EQ(encoder_primary->Encode(frame1, &types), WEBRTC_VIDEO_CODEC_OK);
        EXPECT_EQ(encoder_secondary->Encode(frame2, &types), WEBRTC_VIDEO_CODEC_OK);
    }

    EXPECT_TRUE(callback_primary.WaitForFrames(5));
    EXPECT_TRUE(callback_secondary.WaitForFrames(5));
    const auto& frames_primary = callback_primary.GetFrames();
    const auto& frames_secondary = callback_secondary.GetFrames();
    ASSERT_EQ(frames_primary.size(), 5u);
    ASSERT_EQ(frames_secondary.size(), 5u);
    EXPECT_EQ(frames_primary[0].width, 1280u);
    EXPECT_EQ(frames_primary[0].height, 720u);
    EXPECT_EQ(frames_secondary[0].width, 640u);
    EXPECT_EQ(frames_secondary[0].height, 480u);

    EXPECT_EQ(encoder_primary->Release(), WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(encoder_secondary->Release(), WEBRTC_VIDEO_CODEC_OK);
}

// 16. Negative / Boundary Error Resilience & Recovery
TEST_F(VideoToolboxHardwareTest, FailsGracefullyOnInvalidDimensionsAndRecovers) {
    if (!HasHardwareVideoToolboxH264Support()) {
        GTEST_SKIP() << "Skipping: hardware H.264 VideoToolbox not supported on this host.";
    }

    CapturingEncodedImageCallback callback;
    auto encoder = CreateHardwareEncoder("640c34");
    ASSERT_NE(encoder, nullptr);

    // Encode before InitEncode must return WEBRTC_VIDEO_CODEC_UNINITIALIZED
    auto dummy_buf = CreateSyntheticI420Frame(640, 480, 0);
    webrtc::VideoFrame dummy_frame = webrtc::VideoFrame::Builder()
                                             .set_video_frame_buffer(dummy_buf)
                                             .set_rtp_timestamp(1000)
                                             .build();
    std::vector<webrtc::VideoFrameType> types = {webrtc::VideoFrameType::kVideoFrameKey};
    EXPECT_EQ(encoder->Encode(dummy_frame, &types), WEBRTC_VIDEO_CODEC_UNINITIALIZED);

    // InitEncode with nullptr must fail gracefully
    EXPECT_EQ(encoder->InitEncode(nullptr, default_settings_), WEBRTC_VIDEO_CODEC_ERR_PARAMETER);

    // InitEncode with 0x0 dimensions must fail gracefully without divide-by-zero
    auto zero_settings = CreateDefaultH264CodecSettings(0, 0, 2000, 30);
    EXPECT_LT(encoder->InitEncode(&zero_settings, default_settings_), 0);

    // Encoder must cleanly recover when subsequently initialized with valid 640x480 settings
    EXPECT_EQ(encoder->RegisterEncodeCompleteCallback(&callback), WEBRTC_VIDEO_CODEC_OK);

    auto valid_settings = CreateDefaultH264CodecSettings(640, 480, 2000, 30);
    ASSERT_EQ(encoder->InitEncode(&valid_settings, default_settings_), WEBRTC_VIDEO_CODEC_OK);
    EXPECT_EQ(encoder->Encode(dummy_frame, &types), WEBRTC_VIDEO_CODEC_OK);
    EXPECT_TRUE(callback.WaitForFrames(1));
    EXPECT_EQ(callback.FrameCount(), 1u);

    // Encode with nullptr frame_types must default to delta frame without null dereference
    webrtc::VideoFrame delta_frame = webrtc::VideoFrame::Builder()
                                             .set_video_frame_buffer(dummy_buf)
                                             .set_rtp_timestamp(4000)
                                             .build();
    EXPECT_EQ(encoder->Encode(delta_frame, nullptr), WEBRTC_VIDEO_CODEC_OK);
    EXPECT_TRUE(callback.WaitForFrames(2));
    EXPECT_EQ(callback.FrameCount(), 2u);

    // Re-initializing after Release() without re-registering callback must preserve callback
    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
    ASSERT_EQ(encoder->InitEncode(&valid_settings, default_settings_), WEBRTC_VIDEO_CODEC_OK);
    webrtc::VideoFrame reinit_frame = webrtc::VideoFrame::Builder()
                                              .set_video_frame_buffer(dummy_buf)
                                              .set_rtp_timestamp(7000)
                                              .build();
    EXPECT_EQ(encoder->Encode(reinit_frame, &types), WEBRTC_VIDEO_CODEC_OK);
    EXPECT_TRUE(callback.WaitForFrames(3));
    EXPECT_EQ(callback.FrameCount(), 3u);
    EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

}  // namespace
}  // namespace goldfish::videobridge
