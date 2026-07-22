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

// Disable compiler warnings for external third-party headers. We wrap these in localized
// pragma blocks rather than using target 'copts' so that thread-safety analysis remains
// active on our own local source files.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/peer_connection_interface.h"
#pragma clang diagnostic pop

#include <string>

#include "nlohmann/json.hpp"

namespace goldfish::videobridge {

using RTCConfiguration = ::webrtc::PeerConnectionInterface::RTCConfiguration;

/**
 * @class RtcConfig
 * @brief Utility parser for mapping JSON-based signaling configuration structures into WebRTC
 * configuration classes.
 */
class RtcConfig {
  public:
    /**
     * @brief Parses an nlohmann::json structure containing ICE server configurations.
     * Compatible with standard formats (e.g. Google TURN server format, Twilio format).
     *
     * @param rtc_config The JSON configuration object containing the "iceServers" / "ice_servers"
     * keys.
     * @return RTCConfiguration The fully populated WebRTC config struct.
     */
    static RTCConfiguration Parse(const nlohmann::json& rtc_config);

    /**
     * @brief Parses a serialized JSON string containing ICE server configurations.
     * Fallbacks to empty configuration if the string is empty or invalid JSON.
     *
     * @param rtc_config The raw JSON string to parse.
     * @return RTCConfiguration The populated WebRTC config struct.
     */
    static RTCConfiguration Parse(const std::string& rtc_config);
};

}  // namespace goldfish::videobridge
