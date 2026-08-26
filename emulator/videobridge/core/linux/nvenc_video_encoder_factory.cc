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
#include "core/linux/nvenc_video_encoder_factory.h"

#include <cstring>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/strings/match.h"

#include "core/linux/h264_format_utils.h"
#include "core/linux/nvenc_video_encoder.h"
#include "rtc_base/logging.h"

namespace goldfish::videobridge {

std::unique_ptr<NvencVideoEncoderFactory> NvencVideoEncoderFactory::Create() {
    auto loader_status = NvencLoader::Create();
    if (!loader_status.ok()) {
        return nullptr;
    }
    return std::make_unique<NvencVideoEncoderFactory>(std::move(*loader_status));
}

std::unique_ptr<NvencVideoEncoderFactory> NvencVideoEncoderFactory::CreateForTest(
        std::shared_ptr<NvencLoader> loader) {
    if (!loader) return nullptr;
    return std::make_unique<NvencVideoEncoderFactory>(std::move(loader));
}

NvencVideoEncoderFactory::NvencVideoEncoderFactory(std::shared_ptr<NvencLoader> loader)
        : loader_(std::move(loader)) {
    ProbeHardwareCapabilities();
}

void NvencVideoEncoderFactory::ProbeHardwareCapabilities() {
    if (!loader_) return;

    auto session_params = MakeNvencStruct<NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS>();
    session_params.apiVersion = NVENCAPI_VERSION;
    session_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;

    void* probe_session = nullptr;
    NVENCSTATUS status = loader_->api().nvEncOpenEncodeSessionEx(&session_params, &probe_session);
    if (status != NV_ENC_SUCCESS || !probe_session) {
        RTC_LOG(LS_WARNING) << "Failed to open NVENC probe session: "
                            << NvencStatusToString(status);
        return;
    }

    auto session_cleanup = absl::MakeCleanup(
            [this, probe_session]() { loader_->api().nvEncDestroyEncoder(probe_session); });

    uint32_t guid_count = 0;
    status = loader_->api().nvEncGetEncodeGUIDCount(probe_session, &guid_count);
    if (status == NV_ENC_SUCCESS && guid_count > 0) {
        std::vector<GUID> guids(guid_count);
        loader_->api().nvEncGetEncodeGUIDs(probe_session, guids.data(), guid_count, &guid_count);

        bool h264_supported = false;
        for (const auto& guid : guids) {
            if (std::memcmp(&guid, &NV_ENC_CODEC_H264_GUID, sizeof(GUID)) == 0) {
                h264_supported = true;
                break;
            }
        }

        if (h264_supported) {
            uint32_t profile_count = 0;
            status = loader_->api().nvEncGetEncodeProfileGUIDCount(
                    probe_session, NV_ENC_CODEC_H264_GUID, &profile_count);
            if (status == NV_ENC_SUCCESS && profile_count > 0) {
                std::vector<GUID> profile_guids(profile_count);
                loader_->api().nvEncGetEncodeProfileGUIDs(probe_session, NV_ENC_CODEC_H264_GUID,
                                                          profile_guids.data(), profile_count,
                                                          &profile_count);

                for (const auto& profile_guid : profile_guids) {
                    if (std::memcmp(&profile_guid, &NV_ENC_H264_PROFILE_BASELINE_GUID,
                                    sizeof(GUID)) == 0) {
                        AppendH264ProfileFormats(H264ProfileType::kConstrainedBaseline,
                                                 supported_formats_);
                    } else if (std::memcmp(&profile_guid, &NV_ENC_H264_PROFILE_MAIN_GUID,
                                           sizeof(GUID)) == 0) {
                        AppendH264ProfileFormats(H264ProfileType::kMain, supported_formats_);
                    } else if (std::memcmp(&profile_guid, &NV_ENC_H264_PROFILE_HIGH_GUID,
                                           sizeof(GUID)) == 0) {
                        AppendH264ProfileFormats(H264ProfileType::kHigh, supported_formats_);
                    }
                }
            }
        }
    }
}

std::vector<webrtc::SdpVideoFormat> NvencVideoEncoderFactory::GetSupportedFormats() const {
    return supported_formats_;
}

std::unique_ptr<webrtc::VideoEncoder> NvencVideoEncoderFactory::Create(
        const webrtc::Environment& /*env*/, const webrtc::SdpVideoFormat& format) {
    return CreateVideoEncoder(format);
}

std::unique_ptr<webrtc::VideoEncoder> NvencVideoEncoderFactory::CreateVideoEncoder(
        const webrtc::SdpVideoFormat& format) {
    if (!absl::EqualsIgnoreCase(format.name, "H264")) {
        return nullptr;
    }
    return NvencVideoEncoder::Create(loader_, format);
}

}  // namespace goldfish::videobridge
