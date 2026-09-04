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
#include "core/linux/vaapi_video_encoder_factory.h"

#include <utility>
#include <vector>

#include "absl/strings/match.h"

#include "core/linux/h264_format_utils.h"
#include "core/linux/vaapi_video_encoder.h"
#include "rtc_base/logging.h"

namespace goldfish::videobridge {

std::unique_ptr<VaapiVideoEncoderFactory> VaapiVideoEncoderFactory::Create() {
    auto loader_status = VaapiLoader::Create();
    if (!loader_status.ok()) {
        return nullptr;
    }
    return std::make_unique<VaapiVideoEncoderFactory>(std::move(*loader_status));
}

std::unique_ptr<VaapiVideoEncoderFactory> VaapiVideoEncoderFactory::CreateForTest(
        std::shared_ptr<VaapiLoader> loader) {
    if (!loader) return nullptr;
    return std::make_unique<VaapiVideoEncoderFactory>(std::move(loader));
}

VaapiVideoEncoderFactory::VaapiVideoEncoderFactory(std::shared_ptr<VaapiLoader> loader)
        : loader_(std::move(loader)) {
    ProbeHardwareCapabilities();
}

void VaapiVideoEncoderFactory::ProbeHardwareCapabilities() {
    if (!loader_ || !loader_->display()) return;

    VADisplay dpy = loader_->display();
    int max_profiles = loader_->api().vaMaxNumProfiles(dpy);
    if (max_profiles <= 0) max_profiles = 32;

    std::vector<VAProfile> profile_list(static_cast<size_t>(max_profiles));
    int num_profiles = 0;
    VAStatus status = loader_->api().vaQueryConfigProfiles(dpy, profile_list.data(), &num_profiles);
    if (status != VA_STATUS_SUCCESS || num_profiles <= 0) {
        RTC_LOG(LS_WARNING) << "VaapiVideoEncoderFactory could not query profiles: "
                            << VaapiStatusToString(status);
        return;
    }

    int max_entrypoints = loader_->api().vaMaxNumEntrypoints(dpy);
    if (max_entrypoints <= 0) max_entrypoints = 16;

    std::vector<H264ProfileType> added_profiles;
    auto add_profile_type = [&](H264ProfileType profile_type) {
        for (auto existing : added_profiles) {
            if (existing == profile_type) return;
        }
        added_profiles.push_back(profile_type);
        AppendH264ProfileFormats(profile_type, supported_formats_);
    };

    int profile_count = std::min(num_profiles, max_profiles);
    for (int i = 0; i < profile_count; ++i) {
        VAProfile profile = profile_list[i];
        std::vector<VAEntrypoint> entrypoints(static_cast<size_t>(max_entrypoints));
        int num_entrypoints = 0;
        status = loader_->api().vaQueryConfigEntrypoints(dpy, profile, entrypoints.data(),
                                                         &num_entrypoints);
        if (status != VA_STATUS_SUCCESS) continue;

        bool supports_encode = false;
        int entrypoint_count = std::min(num_entrypoints, max_entrypoints);
        for (int j = 0; j < entrypoint_count; ++j) {
            if (entrypoints[j] == VAEntrypointEncSlice ||
                entrypoints[j] == VAEntrypointEncPicture ||
                entrypoints[j] == VAEntrypointEncSliceLP) {
                supports_encode = true;
                break;
            }
        }
        if (!supports_encode) continue;

        if (profile == VAProfileH264ConstrainedBaseline || profile == VAProfileH264Baseline) {
            add_profile_type(H264ProfileType::kConstrainedBaseline);
        } else if (profile == VAProfileH264Main) {
            add_profile_type(H264ProfileType::kMain);
        } else if (profile == VAProfileH264High) {
            add_profile_type(H264ProfileType::kHigh);
        }
    }
}

std::vector<webrtc::SdpVideoFormat> VaapiVideoEncoderFactory::GetSupportedFormats() const {
    return supported_formats_;
}

std::unique_ptr<webrtc::VideoEncoder> VaapiVideoEncoderFactory::Create(
        const webrtc::Environment& /*env*/, const webrtc::SdpVideoFormat& format) {
    return CreateVideoEncoder(format);
}

std::unique_ptr<webrtc::VideoEncoder> VaapiVideoEncoderFactory::CreateVideoEncoder(
        const webrtc::SdpVideoFormat& format) {
    if (!absl::EqualsIgnoreCase(format.name, "H264")) {
        return nullptr;
    }
    return VaapiVideoEncoder::Create(loader_, format);
}

}  // namespace goldfish::videobridge
