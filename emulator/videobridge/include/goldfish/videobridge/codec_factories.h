#pragma once

#include <memory>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_encoder_factory.h"
#pragma clang diagnostic pop

namespace goldfish::videobridge {

// Creates a video encoder factory that combines the platform's hardware
// accelerators (e.g. VideoToolbox on macOS) with software fallbacks (VP8, VP9, AV1).
std::unique_ptr<webrtc::VideoEncoderFactory> CreatePlatformVideoEncoderFactory();

// Creates a video decoder factory that combines the platform's hardware
// accelerators with software fallbacks.
std::unique_ptr<webrtc::VideoDecoderFactory> CreatePlatformVideoDecoderFactory();

}  // namespace goldfish::videobridge
