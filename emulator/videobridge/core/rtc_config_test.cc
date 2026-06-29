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

#include "rtc_config.h"

#include <gtest/gtest.h>

#include <string>

#include "nlohmann/json.hpp"

namespace goldfish::videobridge {
namespace {

using ::webrtc::PeerConnectionInterface;

static std::string VALID_TURN_CFG = R"#(
{
   "iceServers":[
      {
         "urls":"stun:stun.services.mozilla.com",
         "username":"louis@mozilla.com",
         "credential":"webrtcdemo"
      },
      {
         "urls":[
            "stun:stun.example.com",
            "stun:stun-1.example.com"
         ]
      }
   ]
}
)#";

static std::string VALID_TURN_TWILIO_CFG = R"#(
{
   "username":"dc2d2894d5a9023620c467b0e71cfa6a35457e6679785ed6ae9856fe5bdfa269",
   "ice_servers":[
      {
         "urls":"stun:global.stun.twilio.com:3478"
      },
      {
         "username":"dc2d2894d5a9023620c467b0e71cfa6a35457e6679785ed6ae9856fe5bdfa269",
         "credential":"tE2DajzSJwnsSbc123",
         "urls":"turn:global.turn.twilio.com:3478?transport=udp"
      },
      {
         "username":"dc2d2894d5a9023620c467b0e71cfa6a35457e6679785ed6ae9856fe5bdfa269",
         "credential":"tE2DajzSJwnsSbc123",
         "urls":"turn:global.turn.twilio.com:3478?transport=tcp"
      },
      {
         "username":"dc2d2894d5a9023620c467b0e71cfa6a35457e6679785ed6ae9856fe5bdfa269",
         "credential":"tE2DajzSJwnsSbc123",
         "urls":"turn:global.turn.twilio.com:443?transport=tcp"
      }
   ],
   "date_updated":"Fri, 01 May 2020 01:42:57 +0000",
   "account_sid":"ACXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
   "ttl":"86400",
   "date_created":"Fri, 01 May 2020 01:42:57 +0000",
   "password":"tE2DajzSJwnsSbc123"
})#";

TEST(RtcConfigTest, sets_servers) {
    auto cfg = RtcConfig::Parse(VALID_TURN_CFG);
    EXPECT_EQ(cfg.servers.size(), 2);
    PeerConnectionInterface::IceServer ice = cfg.servers[0];
    EXPECT_EQ(ice.password, "webrtcdemo");
    EXPECT_EQ(ice.username, "louis@mozilla.com");
    EXPECT_EQ(ice.urls.size(), 1);
    EXPECT_EQ(ice.urls[0], "stun:stun.services.mozilla.com");

    // Parses the second ice set
    ice = cfg.servers[1];
    EXPECT_EQ(ice.urls.size(), 2);
    EXPECT_EQ(ice.urls[0], "stun:stun.example.com");
    EXPECT_EQ(ice.urls[1], "stun:stun-1.example.com");
}

TEST(RtcConfigTest, sets_twilio_servers) {
    auto cfg = RtcConfig::Parse(VALID_TURN_TWILIO_CFG);
    EXPECT_EQ(cfg.servers.size(), 4);

    auto ice = cfg.servers[1];
    EXPECT_EQ(ice.urls.size(), 1);
    EXPECT_EQ(ice.urls[0], "turn:global.turn.twilio.com:3478?transport=udp");
    EXPECT_EQ(ice.username, "dc2d2894d5a9023620c467b0e71cfa6a35457e6679785ed6ae9856fe5bdfa269");
    EXPECT_EQ(ice.password, "tE2DajzSJwnsSbc123");
}

TEST(RtcConfigTest, no_ice_servers_use_default) {
    auto cfg = RtcConfig::Parse((std::string) R"#({ "iceCandidatePoolSize" : 5 })#");

    EXPECT_EQ(cfg.servers.size(), 1);

    auto ice = cfg.servers[0];
    EXPECT_EQ(ice.urls.size(), 0);
    EXPECT_EQ(ice.uri, "stun:stun.l.google.com:19302");
}

TEST(RtcConfigTest, set_poolsize) {
    auto cfg = RtcConfig::Parse((std::string) R"#({ "iceCandidatePoolSize" : 5 })#");
    EXPECT_EQ(cfg.ice_candidate_pool_size, 5);
}

TEST(RtcConfigTest, set_bundlePolicy) {
    auto cfg = RtcConfig::Parse((std::string) R"#({ "bundlePolicy" : "balanced" })#");
    EXPECT_EQ(cfg.bundle_policy, PeerConnectionInterface::BundlePolicy::kBundlePolicyBalanced);

    cfg = RtcConfig::Parse((std::string) R"#({ "bundlePolicy" : "max-bundle" })#");
    EXPECT_EQ(cfg.bundle_policy, PeerConnectionInterface::BundlePolicy::kBundlePolicyMaxBundle);

    cfg = RtcConfig::Parse((std::string) R"#({ "bundlePolicy" : "max-compat" })#");
    EXPECT_EQ(cfg.bundle_policy, PeerConnectionInterface::BundlePolicy::kBundlePolicyMaxCompat);
}

TEST(RtcConfigTest, set_type) {
    auto cfg = RtcConfig::Parse((std::string) R"#({ "iceTransportPolicy" : "relay" })#");
    EXPECT_EQ(cfg.type, PeerConnectionInterface::IceTransportsType::kRelay);

    cfg = RtcConfig::Parse((std::string) R"#({ "iceTransportPolicy" : "all" })#");
    EXPECT_EQ(cfg.type, PeerConnectionInterface::IceTransportsType::kAll);
}

TEST(RtcConfigTest, set_rtcpMuxPolicy) {
    auto cfg = RtcConfig::Parse((std::string) R"#({ "rtcpMuxPolicy" : "require" })#");

    EXPECT_EQ(cfg.rtcp_mux_policy, PeerConnectionInterface::RtcpMuxPolicy::kRtcpMuxPolicyRequire);

    cfg = RtcConfig::Parse((std::string) R"#({ "rtcpMuxPolicy" : "negotiate" })#");
    EXPECT_EQ(cfg.rtcp_mux_policy, PeerConnectionInterface::RtcpMuxPolicy::kRtcpMuxPolicyNegotiate);
}

TEST(RtcConfigTest, defaults) {
    auto cfg = RtcConfig::Parse(nlohmann::json{});

    EXPECT_EQ(cfg.rtcp_mux_policy, PeerConnectionInterface::RtcpMuxPolicy::kRtcpMuxPolicyRequire);
    EXPECT_EQ(cfg.servers.size(), 1);
    PeerConnectionInterface::IceServer ice = cfg.servers[0];
    EXPECT_EQ(ice.uri, "stun:stun.l.google.com:19302");
}

}  // namespace
}  // namespace goldfish::videobridge
