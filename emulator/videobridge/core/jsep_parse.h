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
#include "api/jsep.h"
#pragma clang diagnostic pop

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

#include "nlohmann/json.hpp"

namespace goldfish::videobridge::internal {

/**
 * @brief Non-throwing parsers for untrusted JSEP signaling JSON.
 *
 * Validates and converts raw JSON into WebRTC session descriptions and ICE
 * candidates.
 */

/// A remote ICE candidate whose fields have been checked.
struct IceCandidate {
    std::string sdp_mid;    ///< The media stream identifier (e.g. "0", "audio").
    int sdp_mline_index;    ///< 0-based index of the m-line association.
    std::string candidate;  ///< The raw candidate SDP string.
};

/**
 * @brief Validates an ICE candidate payload.
 *
 * @param msg The candidate object.
 * @return The candidate, or InvalidArgument if a field is missing or is not
 *         of the type the JSEP wire format requires.
 */
absl::StatusOr<IceCandidate> ParseIceCandidate(const nlohmann::json& msg);

/**
 * @brief Validates a session description payload and builds it.
 *
 * @param msg An object carrying 'type' and 'sdp' string fields.
 * @return The parsed description, InvalidArgument if the payload is
 *         malformed or the SDP does not parse, or Unimplemented for a
 *         loopback offer.
 */
absl::StatusOr<std::unique_ptr<::webrtc::SessionDescriptionInterface>> ParseSdpMessage(
        const nlohmann::json& msg);

/**
 * @brief Unwraps a JSEP payload that may be nested inside a same-named field.
 *
 * Clients send both shapes: `{"sdp": {"type": ..., "sdp": ...}}` and the flat
 * `{"type": ..., "sdp": ...}`. Returns whichever object the handler should
 * actually read.
 *
 * @param msg The incoming message.
 * @param key The field to unwrap, "sdp" or "candidate".
 * @return A reference into `msg`; valid only as long as `msg` is.
 */
const nlohmann::json& UnwrapEnvelope(const nlohmann::json& msg, absl::string_view key);

}  // namespace goldfish::videobridge::internal
