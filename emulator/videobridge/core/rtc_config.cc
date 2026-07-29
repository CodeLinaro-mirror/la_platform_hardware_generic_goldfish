#include "rtc_config.h"

#include <string_view>
#include <vector>

#include "absl/log/log.h"

namespace goldfish::videobridge {

using IceServer = ::webrtc::PeerConnectionInterface::IceServer;

namespace {
constexpr std::string_view kDefaultStunUri = "stun:stun.l.google.com:19302";
constexpr std::string_view kIceCandidatePoolSize = "iceCandidatePoolSize";
constexpr std::string_view kBundlePolicy = "bundlePolicy";
constexpr std::string_view kIceTransportPolicy = "iceTransportPolicy";
constexpr std::string_view kRtcpMuxPolicy = "rtcpMuxPolicy";

IceServer ParseIce(const nlohmann::json& desc) {
    IceServer ice;
    ice.password = desc.value("credential", "");
    ice.username = desc.value("username", "");

    if (desc.contains("urls")) {
        const auto& urls = desc["urls"];
        if (urls.is_string()) {
            ice.urls.push_back(urls.get<std::string>());
        } else if (urls.is_array()) {
            for (const auto& uri : urls) {
                if (uri.is_string()) {
                    ice.urls.push_back(uri.get<std::string>());
                }
            }
        }
    }
    return ice;
}
}  // namespace

RTCConfiguration RtcConfig::Parse(const nlohmann::json& rtc_config) {
    RTCConfiguration configuration;

    const auto& ice_servers = rtc_config.contains("iceServers")    ? rtc_config["iceServers"]
                              : rtc_config.contains("ice_servers") ? rtc_config["ice_servers"]
                                                                   : nlohmann::json::array();

    if (!ice_servers.empty()) {
        for (const auto& ice_server : ice_servers) {
            configuration.servers.push_back(ParseIce(ice_server));
        }
    } else {
        LOG(WARNING) << "RTC Configuration does not specify any iceServers. A default STUN server "
                        "will be added as a fallback.";
    }

    // Modern webrtc with multiple audio & video channels.
    configuration.sdp_semantics = ::webrtc::SdpSemantics::kUnifiedPlan;

    // ice candidate pool size
    if (rtc_config.contains(kIceCandidatePoolSize) &&
        rtc_config[kIceCandidatePoolSize].is_number_integer()) {
        configuration.ice_candidate_pool_size = rtc_config[kIceCandidatePoolSize];
    }

    // bundle policy
    if (rtc_config.contains(kBundlePolicy)) {
        const std::string policy = rtc_config[kBundlePolicy];
        if (policy == "balanced") {
            configuration.bundle_policy =
                    ::webrtc::PeerConnectionInterface::BundlePolicy::kBundlePolicyBalanced;
        } else if (policy == "max-compat") {
            configuration.bundle_policy =
                    ::webrtc::PeerConnectionInterface::BundlePolicy::kBundlePolicyMaxCompat;
        } else if (policy == "max-bundle") {
            configuration.bundle_policy =
                    ::webrtc::PeerConnectionInterface::BundlePolicy::kBundlePolicyMaxBundle;
        }
    }

    if (rtc_config.contains(kIceTransportPolicy)) {
        const std::string policy = rtc_config[kIceTransportPolicy];
        if (policy == "relay") {
            configuration.type = ::webrtc::PeerConnectionInterface::IceTransportsType::kRelay;
        } else if (policy == "all") {
            configuration.type = ::webrtc::PeerConnectionInterface::IceTransportsType::kAll;
        }
    }

    if (rtc_config.contains(kRtcpMuxPolicy)) {
        const std::string policy = rtc_config[kRtcpMuxPolicy];
        if (policy == "require") {
            configuration.rtcp_mux_policy =
                    ::webrtc::PeerConnectionInterface::RtcpMuxPolicy::kRtcpMuxPolicyRequire;
        } else if (policy == "negotiate") {
            configuration.rtcp_mux_policy =
                    ::webrtc::PeerConnectionInterface::RtcpMuxPolicy::kRtcpMuxPolicyNegotiate;
        }
    }
    // Let's add at least a default stun server if none is present.
    if (configuration.servers.empty()) {
        IceServer server;
        server.uri = kDefaultStunUri;
        configuration.servers.push_back(server);
    }
    return configuration;
}

RTCConfiguration RtcConfig::Parse(const std::string& rtc_config) {
    const nlohmann::json config = nlohmann::json::parse(rtc_config, nullptr, false);
    if (!config.is_discarded()) {
        return Parse(config);
    }
    return {};
}

}  // namespace goldfish::videobridge
