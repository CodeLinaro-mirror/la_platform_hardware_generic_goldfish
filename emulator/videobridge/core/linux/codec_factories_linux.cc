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
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "goldfish/videobridge/codec_factories.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "absl/strings/ascii.h"

#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#pragma clang diagnostic pop

#include "core/linux/nvenc_video_encoder_factory.h"
#include "core/linux/vaapi_video_encoder_factory.h"
#include "rtc_base/logging.h"

namespace goldfish::videobridge {
namespace {

class CompositeVideoEncoderFactory : public webrtc::VideoEncoderFactory {
  public:
    CompositeVideoEncoderFactory(
            std::vector<std::unique_ptr<webrtc::VideoEncoderFactory>> hardware_factories,
            std::unique_ptr<webrtc::VideoEncoderFactory> software_factory)
            : hardware_factories_(std::move(hardware_factories))
            , software_factory_(std::move(software_factory)) {}

    std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
        std::vector<webrtc::SdpVideoFormat> formats;
        for (const auto& hw_factory : hardware_factories_) {
            if (hw_factory) {
                auto hw_formats = hw_factory->GetSupportedFormats();
                for (const auto& hw_fmt : hw_formats) {
                    bool already_present = false;
                    for (const auto& fmt : formats) {
                        if (fmt.IsSameCodec(hw_fmt)) {
                            already_present = true;
                            break;
                        }
                    }
                    if (!already_present) {
                        formats.push_back(hw_fmt);
                    }
                }
            }
        }
        if (software_factory_) {
            auto sw_formats = software_factory_->GetSupportedFormats();
            for (const auto& sw_fmt : sw_formats) {
                bool already_present = false;
                for (const auto& fmt : formats) {
                    if (fmt.IsSameCodec(sw_fmt)) {
                        already_present = true;
                        break;
                    }
                }
                if (!already_present) {
                    formats.push_back(sw_fmt);
                }
            }
        }
        return formats;
    }

    std::unique_ptr<webrtc::VideoEncoder> Create(const webrtc::Environment& env,
                                                 const webrtc::SdpVideoFormat& format) override {
        for (const auto& hw_factory : hardware_factories_) {
            if (hw_factory) {
                for (const auto& hw_format : hw_factory->GetSupportedFormats()) {
                    if (hw_format.IsSameCodec(format)) {
                        auto encoder = hw_factory->Create(env, format);
                        if (encoder) return encoder;
                        RTC_LOG(LS_WARNING)
                                << "Hardware encoder creation failed for format '" << format.name
                                << "'. Trying next hardware backend in chain.";
                    }
                }
            }
        }
        if (software_factory_) {
            return software_factory_->Create(env, format);
        }
        return nullptr;
    }

  private:
    std::vector<std::unique_ptr<webrtc::VideoEncoderFactory>> hardware_factories_;
    std::unique_ptr<webrtc::VideoEncoderFactory> software_factory_;
};

}  // namespace

std::unique_ptr<webrtc::VideoEncoderFactory> CreatePlatformVideoEncoderFactory() {
    std::vector<std::unique_ptr<webrtc::VideoEncoderFactory>> hardware_factories;

    const char* encoder_env = std::getenv("ANDROID_EMU_VIDEO_ENCODER");
    std::string override_backend = encoder_env ? absl::AsciiStrToLower(encoder_env) : "";

    auto try_add_nvenc = [&]() {
        auto nvenc_factory = NvencVideoEncoderFactory::Create();
        if (nvenc_factory && !nvenc_factory->GetSupportedFormats().empty()) {
            RTC_LOG(LS_INFO) << "Registered NVIDIA NVENC hardware video encoder.";
            hardware_factories.push_back(std::move(nvenc_factory));
        }
    };

    auto try_add_vaapi = [&]() {
        auto vaapi_factory = VaapiVideoEncoderFactory::Create();
        if (vaapi_factory && !vaapi_factory->GetSupportedFormats().empty()) {
            RTC_LOG(LS_INFO) << "Registered Intel/AMD VA-API hardware video encoder.";
            hardware_factories.push_back(std::move(vaapi_factory));
        }
    };

    if (override_backend == "nvenc" || override_backend == "nvidia") {
        RTC_LOG(LS_INFO) << "ANDROID_EMU_VIDEO_ENCODER explicitly configured to 'nvenc'.";
        try_add_nvenc();
    } else if (override_backend == "vaapi" || override_backend == "intel" ||
               override_backend == "amd") {
        RTC_LOG(LS_INFO) << "ANDROID_EMU_VIDEO_ENCODER explicitly configured to 'vaapi'.";
        try_add_vaapi();
    } else if (override_backend == "software" || override_backend == "sw" ||
               override_backend == "builtin") {
        RTC_LOG(LS_INFO) << "ANDROID_EMU_VIDEO_ENCODER explicitly configured to 'software'.";
    } else {
        // Default 'auto': Chained multi-hardware failover (NVENC -> VA-API -> Software)
        try_add_nvenc();
        try_add_vaapi();
    }

    if (hardware_factories.empty() && override_backend != "software" && override_backend != "sw" &&
        override_backend != "builtin") {
        RTC_LOG(LS_INFO)
                << "No hardware video encoders available. Using software encoder fallback.";
    }

    auto software_factory = webrtc::CreateBuiltinVideoEncoderFactory();
    return std::make_unique<CompositeVideoEncoderFactory>(std::move(hardware_factories),
                                                          std::move(software_factory));
}

std::unique_ptr<webrtc::VideoDecoderFactory> CreatePlatformVideoDecoderFactory() {
    return webrtc::CreateBuiltinVideoDecoderFactory();
}

}  // namespace goldfish::videobridge
