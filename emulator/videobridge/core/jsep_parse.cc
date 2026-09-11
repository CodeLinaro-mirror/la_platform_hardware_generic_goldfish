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

#include "jsep_parse.h"

#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace goldfish::videobridge::internal {

absl::StatusOr<IceCandidate> ParseIceCandidate(const nlohmann::json& msg) {
    if (!msg.contains("sdpMid") || !msg.contains("sdpMLineIndex") || !msg.contains("candidate")) {
        return absl::InvalidArgumentError(
                "JSON missing required properties ('sdpMid', 'sdpMLineIndex', or "
                "'candidate')");
    }
    if (!msg["sdpMid"].is_string() || !msg["sdpMLineIndex"].is_number_integer() ||
        !msg["candidate"].is_string()) {
        return absl::InvalidArgumentError(
                "JSON properties have invalid types (expected string, int, string)");
    }
    return IceCandidate{
        msg["sdpMid"].get<std::string>(),
        msg["sdpMLineIndex"].get<int>(),
        msg["candidate"].get<std::string>(),
    };
}

absl::StatusOr<std::unique_ptr<::webrtc::SessionDescriptionInterface>> ParseSdpMessage(
        const nlohmann::json& msg) {
    if (!msg.contains("type") || !msg.contains("sdp")) {
        return absl::InvalidArgumentError("SDP message missing required 'type' or 'sdp' fields.");
    }
    const std::string type = msg["type"];
    const std::string sdp = msg["sdp"];

    if (type == "offer-loopback") {
        return absl::UnimplementedError("Loopback offers are not supported by this bridge.");
    }

    auto sdp_type_opt = ::webrtc::SdpTypeFromString(type);
    if (!sdp_type_opt) {
        return absl::InvalidArgumentError(absl::StrCat("Invalid JSEP message type: '", type, "'"));
    }

    ::webrtc::SdpParseError error;
    auto session_description = ::webrtc::CreateSessionDescription(*sdp_type_opt, sdp, &error);
    if (!session_description) {
        return absl::InvalidArgumentError(absl::StrCat("SDP parse failed: ", error.description));
    }
    return session_description;
}

const nlohmann::json& UnwrapEnvelope(const nlohmann::json& msg, absl::string_view key) {
    const std::string field(key);
    if (msg.contains(field) && msg[field].is_object() && msg[field].contains(field)) {
        return msg[field];
    }
    return msg;
}

}  // namespace goldfish::videobridge::internal
