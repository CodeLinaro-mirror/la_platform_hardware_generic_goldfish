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
#include "api/data_channel_interface.h"
#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#pragma clang diagnostic pop

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/videobridge/input_sender.h"
#include "nlohmann/json.hpp"

namespace goldfish::videobridge {

using IceCandidatePtr = std::unique_ptr<::webrtc::IceCandidateInterface>;
using SessionDescriptionPtr = std::unique_ptr<::webrtc::SessionDescriptionInterface>;

class RtcConnection;
class EventForwarder;
class MediaProvider;

/**
 * @class EmptyConnectionObserver
 * @brief Utility observer implementation providing empty no-op defaults for WebRTC's
 * PeerConnectionObserver.
 *
 * This allows subclasses like Participant to override only the relevant observer callbacks
 * rather than writing verbose boilerplate overrides for every WebRTC event.
 */
class EmptyConnectionObserver : public ::webrtc::PeerConnectionObserver {
  public:
    void OnSignalingChange(::webrtc::PeerConnectionInterface::SignalingState new_state) override {}
    void OnRenegotiationNeeded() override {}
    void OnIceGatheringChange(
            ::webrtc::PeerConnectionInterface::IceGatheringState new_state) override {}
    void OnIceCandidate(const ::webrtc::IceCandidateInterface* candidate) override {}
};

/**
 * @class Participant
 * @brief Manages the PeerConnection session, event forwarding, and JSEP state machine for a single
 * client.
 *
 * This class handles:
 * - Parsing SDP offers and creating SDP answers during connection negotiation.
 * - Establishing input-forwarding data channels (mouse, keyboard, touch) and hooking up
 * InputSenders.
 * - Processing incoming ICE candidates and outputting local ICE candidates.
 * - Injecting media tracks via MediaProvider.
 */
class Participant : public EmptyConnectionObserver,
                    public std::enable_shared_from_this<Participant> {
  public:
    /**
     * @brief Constructs a Participant session.
     *
     * @param connection The parent RtcConnection context coordinating threads and factory.
     * @param identity The unique identifier of this client/session.
     * @param rtc_config The signaling JSON configuration containing TURN settings.
     * @param media_provider Injected provider to set up media tracks on connection.
     */
    Participant(RtcConnection& connection, std::string identity, nlohmann::json rtc_config,
                std::shared_ptr<MediaProvider> media_provider = nullptr);
    ~Participant() override;

    /**
     * @brief Initializes the PeerConnection and creates default control data channels.
     * Must be called on the WebRTC signaling thread.
     *
     * @return absl::Status indicating success or failure.
     */
    absl::Status Initialize();

    /**
     * @brief Processes an incoming JSEP signaling JSON message (candidate or SDP description) from
     * the client.
     *
     * @param msg The parsed JSON payload.
     */
    void IncomingMessage(const nlohmann::json& msg);

    /**
     * @brief Closes the participant PeerConnection session and stops all forwarding.
     * Safe to call from any thread (routes calls to the signaling thread).
     */
    void Close();

    /**
     * @brief Blocks the calling thread until this participant is completely closed.
     */
    void WaitForClose();

    /**
     * @brief Returns the unique client identifier for this participant.
     */
    const std::string& GetPeerId() const { return peer_id_; }

    // PeerConnectionObserver overrides
    /**
     * @brief Called by WebRTC when a new local ICE candidate has been gathered.
     */
    void OnIceCandidate(const ::webrtc::IceCandidateInterface* candidate) override;

    /**
     * @brief Called by WebRTC when the peer connection state changes (e.g. connected, closed,
     * failed).
     */
    void OnConnectionChange(
            ::webrtc::PeerConnectionInterface::PeerConnectionState new_state) override;

    /**
     * @brief Called by WebRTC when the ICE connection state changes.
     */
    void OnIceConnectionChange(
            ::webrtc::PeerConnectionInterface::IceConnectionState new_state) override;

    /**
     * @brief Called by WebRTC when the remote side opens a data channel.
     */
    void OnDataChannel(webrtc::scoped_refptr<::webrtc::DataChannelInterface> channel) override;

  private:
    friend class SetRemoteDescriptionCallback;

    void DoClose();
    absl::Status DoInitialize();
    void SendMessage(const nlohmann::json& msg);
    void DoIncomingMessage(const nlohmann::json& msg);
    void HandleOffer(const nlohmann::json& msg);
    void HandleCandidate(const nlohmann::json& msg);
    absl::Status CreatePeerConnection(const nlohmann::json& rtc_configuration);
    absl::Status AddDataChannel(DataChannelLabel label);
    absl::StatusOr<SessionDescriptionPtr> ParseSdpMessage(const nlohmann::json& msg);
    void ReceivedSessionDescription(::webrtc::SessionDescriptionInterface* desc);
    void OnRemoteDescriptionSet();

    RtcConnection& connection_;
    std::string peer_id_;
    nlohmann::json rtc_config_;
    std::shared_ptr<MediaProvider> media_provider_;

    webrtc::scoped_refptr<::webrtc::PeerConnectionInterface> peer_connection_;
    absl::flat_hash_map<DataChannelLabel, std::unique_ptr<EventForwarder>> event_forwarders_;
    absl::flat_hash_map<std::string, webrtc::scoped_refptr<::webrtc::DataChannelInterface>>
            data_channels_;

    std::atomic<bool> closed_{false};
    absl::Mutex closed_mutex_;

    bool remote_description_set_ = false;
    std::vector<IceCandidatePtr> queued_candidates_;
};

}  // namespace goldfish::videobridge
