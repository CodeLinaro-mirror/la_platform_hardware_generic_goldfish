#include "goldfish/videobridge/codec_factories.h"

#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"

namespace goldfish::videobridge {

std::unique_ptr<webrtc::VideoEncoderFactory> CreatePlatformVideoEncoderFactory() {
    // Falls back to WebRTC's built-in software encoders (VP8, VP9, AV1)
    return webrtc::CreateBuiltinVideoEncoderFactory();
}

std::unique_ptr<webrtc::VideoDecoderFactory> CreatePlatformVideoDecoderFactory() {
    // Falls back to WebRTC's built-in software decoders (VP8, VP9, AV1)
    return webrtc::CreateBuiltinVideoDecoderFactory();
}

}  // namespace goldfish::videobridge
