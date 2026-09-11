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

// Every payload here arrives from a signaling client, so none of it can be
// trusted. The parsers must be total: any JSON at all yields a value or a
// status, never an exception and never a crash.
//
// These run without a PeerConnection, which is what makes it affordable to
// enumerate the malformed cases rather than sampling them.

#include "jsep_parse.h"

#include <gtest/gtest.h>

#include <string>

#include "absl/status/status.h"

#include "nlohmann/json.hpp"

namespace goldfish::videobridge::internal {
namespace {

constexpr char kValidSdp[] =
        "v=0\r\n"
        "o=- 4611731400430051336 2 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        "a=group:BUNDLE 0\r\n"
        "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=ice-ufrag:tOQd\r\n"
        "a=ice-pwd:k9dNCBLXQGDoiFsCPHTsHFTh\r\n"
        "a=fingerprint:sha-256 "
        "39:4A:09:1E:0E:33:32:85:51:03:49:81:38:8C:44:4C:2E:C4:C4:BD:1B:2E:9C:00:F7:F9:F8:2C:AA:"
        "1C:D8:97\r\n"
        "a=setup:actpass\r\n"
        "a=mid:0\r\n"
        "a=sctp-port:5000\r\n";

// ---------------------------------------------------------------------------
// Malformed input must never escape as an exception.
// ---------------------------------------------------------------------------

// Payloads whose field types violate the JSEP wire format. nlohmann throws on
// an implicit conversion from the wrong type, and an exception escaping into
// the WebRTC signaling thread terminates the process, so each of these is a
// remotely triggerable abort if it is not rejected up front.
class MalformedSdpTest : public testing::TestWithParam<const char*> {};

TEST_P(MalformedSdpTest, IsRejectedRatherThanThrown) {
    const nlohmann::json msg = nlohmann::json::parse(GetParam());
    const auto result = ParseSdpMessage(msg);
    EXPECT_FALSE(result.ok()) << "Accepted a malformed payload: " << GetParam();
}

INSTANTIATE_TEST_SUITE_P(MissingFields, MalformedSdpTest,
                         testing::Values(R"({})", R"({"type": "offer"})", R"({"sdp": "v=0"})",
                                         R"({"unrelated": "value"})"));

INSTANTIATE_TEST_SUITE_P(UnusableValues, MalformedSdpTest,
                         testing::Values(
                                 // A string, but not a JSEP description type.
                                 R"({"type": "nonsense", "sdp": "v=0"})",
                                 R"({"type": "", "sdp": "v=0"})",
                                 // A plausible type, but the body is not SDP.
                                 R"({"type": "offer", "sdp": "not sdp at all"})",
                                 R"({"type": "offer", "sdp": ""})"));

// A non-object payload reaches the parser whenever a client sends a bare JSON
// value. contains() is well defined on these, so they must fall out as errors.
TEST(ParseSdpMessageTest, RejectsPayloadsThatAreNotObjects) {
    for (const char* raw : {"42", "\"a string\"", "null", "true", "[1, 2, 3]"}) {
        const nlohmann::json msg = nlohmann::json::parse(raw);
        EXPECT_FALSE(ParseSdpMessage(msg).ok()) << "Accepted non-object payload: " << raw;
    }
}

TEST(ParseSdpMessageTest, AcceptsAWellFormedOffer) {
    const nlohmann::json msg = {{"type", "offer"}, {"sdp", kValidSdp}};
    const auto result = ParseSdpMessage(msg);
    ASSERT_TRUE(result.ok()) << result.status();
    EXPECT_EQ((*result)->GetType(), ::webrtc::SdpType::kOffer);
}

TEST(ParseSdpMessageTest, ReportsLoopbackOffersAsUnimplemented) {
    const nlohmann::json msg = {{"type", "offer-loopback"}, {"sdp", kValidSdp}};
    const auto result = ParseSdpMessage(msg);
    ASSERT_FALSE(result.ok());
    // Distinct from InvalidArgument: the payload is understood, just refused.
    EXPECT_EQ(result.status().code(), absl::StatusCode::kUnimplemented);
}

// ---------------------------------------------------------------------------
// ICE candidates
// ---------------------------------------------------------------------------

class MalformedCandidateTest : public testing::TestWithParam<const char*> {};

TEST_P(MalformedCandidateTest, IsRejectedRatherThanThrown) {
    const nlohmann::json msg = nlohmann::json::parse(GetParam());
    EXPECT_FALSE(ParseIceCandidate(msg).ok()) << "Accepted a malformed candidate: " << GetParam();
}

INSTANTIATE_TEST_SUITE_P(
        WrongFieldTypes, MalformedCandidateTest,
        testing::Values(R"({"sdpMid": 0, "sdpMLineIndex": 0, "candidate": "c"})",
                        R"({"sdpMid": "0", "sdpMLineIndex": "0", "candidate": "c"})",
                        R"({"sdpMid": "0", "sdpMLineIndex": 0, "candidate": 5})",
                        R"({"sdpMid": null, "sdpMLineIndex": null, "candidate": null})",
                        // A float is a number but not an m-line index.
                        R"({"sdpMid": "0", "sdpMLineIndex": 1.5, "candidate": "c"})"));

INSTANTIATE_TEST_SUITE_P(MissingFields, MalformedCandidateTest,
                         testing::Values(R"({})", R"({"sdpMid": "0"})",
                                         R"({"sdpMid": "0", "sdpMLineIndex": 0})",
                                         R"({"candidate": "c"})"));

TEST(ParseIceCandidateTest, AcceptsAWellFormedCandidate) {
    const nlohmann::json msg = {
        {"sdpMid", "0"}, {"sdpMLineIndex", 3}, {"candidate", "candidate:1 1 udp 1 1.2.3.4 1"}};
    const auto result = ParseIceCandidate(msg);
    ASSERT_TRUE(result.ok()) << result.status();
    EXPECT_EQ(result->sdp_mid, "0");
    EXPECT_EQ(result->sdp_mline_index, 3);
    EXPECT_EQ(result->candidate, "candidate:1 1 udp 1 1.2.3.4 1");
}

TEST(ParseIceCandidateTest, RejectsPayloadsThatAreNotObjects) {
    for (const char* raw : {"42", "\"a string\"", "null", "[1, 2, 3]"}) {
        const nlohmann::json msg = nlohmann::json::parse(raw);
        EXPECT_FALSE(ParseIceCandidate(msg).ok()) << "Accepted non-object payload: " << raw;
    }
}

// ---------------------------------------------------------------------------
// Envelope shapes
// ---------------------------------------------------------------------------

TEST(UnwrapEnvelopeTest, UnwrapsANestedPayload) {
    const nlohmann::json msg = {{"sdp", {{"type", "offer"}, {"sdp", "v=0"}}}};
    const nlohmann::json& inner = UnwrapEnvelope(msg, "sdp");
    EXPECT_EQ(inner["type"], "offer");
    EXPECT_EQ(inner["sdp"], "v=0");
}

TEST(UnwrapEnvelopeTest, PassesAFlatPayloadThrough) {
    const nlohmann::json msg = {{"type", "offer"}, {"sdp", "v=0"}};
    EXPECT_EQ(&UnwrapEnvelope(msg, "sdp"), &msg);
}

// A nested object that does not itself carry the key is not an envelope, so
// the whole message is what the handler should see.
TEST(UnwrapEnvelopeTest, TreatsAnObjectWithoutTheKeyAsFlat) {
    const nlohmann::json msg = {{"sdp", {{"unrelated", "value"}}}};
    EXPECT_EQ(&UnwrapEnvelope(msg, "sdp"), &msg);
}

TEST(UnwrapEnvelopeTest, PassesThroughWhenTheKeyIsAbsent) {
    const nlohmann::json msg = {{"other", 1}};
    EXPECT_EQ(&UnwrapEnvelope(msg, "sdp"), &msg);
}

TEST(UnwrapEnvelopeTest, PassesThroughWhenTheValueIsNotAnObject) {
    const nlohmann::json msg = {{"sdp", "v=0"}};
    EXPECT_EQ(&UnwrapEnvelope(msg, "sdp"), &msg);
}

}  // namespace
}  // namespace goldfish::videobridge::internal
