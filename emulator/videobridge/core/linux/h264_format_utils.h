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
#pragma once

#include <string>
#include <utility>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/video_codecs/h264_profile_level_id.h"
#include "api/video_codecs/sdp_video_format.h"
#pragma clang diagnostic pop

namespace goldfish::videobridge {

enum class H264ProfileType {
    kConstrainedBaseline,
    kMain,
    kHigh,
};

/**
 * @brief Appends standard WebRTC H.264 SdpVideoFormat entries (Level 3.1 & 5.2, Packetization modes
 * 1 & 0) for the given profile type.
 */
inline void AppendH264ProfileFormats(H264ProfileType profile,
                                     std::vector<webrtc::SdpVideoFormat>& formats) {
    auto add_format = [&](const std::string& profile_level_id,
                          const std::string& packetization_mode) {
        webrtc::SdpVideoFormat format("H264");
        format.parameters["level-asymmetry-allowed"] = "1";
        format.parameters["packetization-mode"] = packetization_mode;
        format.parameters["profile-level-id"] = profile_level_id;
        formats.push_back(std::move(format));
    };

    const std::vector<std::string> profile_levels = [&]() -> std::vector<std::string> {
        switch (profile) {
        case H264ProfileType::kConstrainedBaseline:
            // Constrained Baseline Profile (CBP) and Baseline (BP)
            return {"42e01f", "42e034", "42001f", "420034"};
        case H264ProfileType::kMain:
            // Main Profile (MP)
            return {"4d001f", "4d0034"};
        case H264ProfileType::kHigh:
            // High Profile (HP)
            return {"640c1f", "640c34"};
        }
    }();

    for (const auto& level : profile_levels) {
        add_format(level, "1");
        add_format(level, "0");
    }
}

}  // namespace goldfish::videobridge
