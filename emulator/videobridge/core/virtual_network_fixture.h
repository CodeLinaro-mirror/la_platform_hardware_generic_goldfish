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

// Test-only scaffolding for driving real participants over a network that does
// not exist. Nothing here binds a port or enumerates a host interface, so the
// tests built on it are hermetic and do not care what machine they run on.

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "rtc_base/fake_network.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/thread.h"
#include "rtc_base/virtual_socket_server.h"
#pragma clang diagnostic pop

#include <memory>
#include <string>

#include "absl/time/time.h"

#include "goldfish/videobridge/switchboard.h"
#include "nlohmann/json.hpp"

namespace goldfish::videobridge {

// An address that exists only inside these tests. A candidate carrying it
// cannot have come from a real adapter.
inline constexpr char kFakeInterface[] = "192.168.13.37";

// A minimal data channel offer. A participant only needs enough of one to
// reach SetLocalDescription, which is what starts candidate gathering.
inline constexpr char kDataChannelOffer[] =
        "v=0\r\n"
        "o=- 4611731400430051336 2 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        "a=group:BUNDLE 0\r\n"
        "a=msid-semantic: WMS\r\n"
        "m=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=ice-ufrag:tOQd\r\n"
        "a=ice-pwd:k9dNCBLXQGDoiFsCPHTsHFTh\r\n"
        "a=ice-options:trickle\r\n"
        "a=fingerprint:sha-256 "
        "39:4A:09:1E:0E:33:32:85:51:03:49:81:38:8C:44:4C:2E:C4:C4:BD:1B:2E:9C:00:F7:F9:F8:2C:AA:"
        "1C:D8:97\r\n"
        "a=setup:actpass\r\n"
        "a=mid:0\r\n"
        "a=sctp-port:5000\r\n"
        "a=max-message-size:262144\r\n";

/**
 * @brief A Switchboard whose participants run on a network that does not exist.
 *
 * Everything below the signaling layer is real: real participants, real
 * PeerConnections, real SDP. Only the socket layer is substituted.
 */
class VirtualSwitchboard : public Switchboard {
  public:
    VirtualSwitchboard() : Switchboard(nullptr, nullptr, MakeVirtualSubstrate()) {}

    // Exposed so that tests can construct a Participant directly against this
    // connection rather than going through Connect().
    static NetworkSubstrate MakeVirtualSubstrate() {
        return NetworkSubstrate{
            .socket_server = std::make_unique<webrtc::VirtualSocketServer>(),
            .network_manager =
                    [](webrtc::Thread* network_thread) {
                        auto manager = std::make_unique<webrtc::FakeNetworkManager>(network_thread);
                        manager->AddInterface(webrtc::SocketAddress(kFakeInterface, 0));
                        return manager;
                    },
        };
    }
};

// Drains signaling until `predicate` accepts a message, or the budget runs out.
// The budget bounds failure, not success: a passing run returns as soon as the
// message it wants arrives.
inline nlohmann::json AwaitMessage(Switchboard& board, const std::string& id,
                                   bool (*predicate)(const nlohmann::json&)) {
    const absl::Time deadline = absl::Now() + absl::Seconds(10);
    while (absl::Now() < deadline) {
        auto next = board.NextMessage(id, absl::Milliseconds(200));
        if (!next.ok()) {
            continue;
        }
        auto parsed = nlohmann::json::parse(*next, nullptr, false);
        if (!parsed.is_discarded() && predicate(parsed)) {
            return parsed;
        }
    }
    return nlohmann::json::object();
}

}  // namespace goldfish::videobridge
