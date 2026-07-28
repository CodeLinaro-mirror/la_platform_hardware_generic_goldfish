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

#include "participant.h"

#include <utility>
// Disable compiler warnings for external third-party headers. We wrap these in localized
// pragma blocks rather than using target 'copts' so that thread-safety analysis remains
// active on our own local source files.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/environment/environment_factory.h"
#include "api/jsep.h"
#include "p2p/client/basic_port_allocator.h"
#include "rtc_base/ref_counted_object.h"
#pragma clang diagnostic pop

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "event_forwarder.h"
#include "goldfish/videobridge/media_provider.h"
#include "goldfish/videobridge/rtc_connection.h"
#include "rtc_config.h"

namespace goldfish::videobridge {

class SetRemoteDescriptionCallback : public ::webrtc::SetRemoteDescriptionObserverInterface {
  public:
    explicit SetRemoteDescriptionCallback(std::weak_ptr<Participant> participant)
            : participant_(std::move(participant)) {}

    void OnSetRemoteDescriptionComplete(::webrtc::RTCError error) override {
        auto self = participant_.lock();
        if (!self) {
            VLOG(1) << "Participant already destroyed. Dropping SetRemoteDescription callback.";
            return;
        }
        if (!error.ok()) {
            LOG(ERROR) << "WebRTC SetRemoteDescription failed for participant " << self->peer_id_
                       << ": " << error.message();
            return;
        }
        self->OnRemoteDescriptionSet();
    }

  private:
    std::weak_ptr<Participant> participant_;
};

struct IceCandidate {
    std::string sdp_mid;    ///< The media stream identifier (e.g. "0", "1", "audio", "video").
    int sdp_mline_index;    ///< The index (0-based) of the m-line association.
    std::string candidate;  ///< The raw candidate SDP string (e.g., "candidate:842163049 1 ...").

    static absl::StatusOr<IceCandidate> FromJson(const nlohmann::json& json_candidate) {
        if (!json_candidate.contains("sdpMid") || !json_candidate.contains("sdpMLineIndex") ||
            !json_candidate.contains("candidate")) {
            return absl::InvalidArgumentError(
                    "JSON missing required properties ('sdpMid', 'sdpMLineIndex', or "
                    "'candidate')");
        }
        if (!json_candidate["sdpMid"].is_string() ||
            !json_candidate["sdpMLineIndex"].is_number_integer() ||
            !json_candidate["candidate"].is_string()) {
            return absl::InvalidArgumentError(
                    "JSON properties have invalid types (expected string, int, string)");
        }
        return IceCandidate{
            json_candidate["sdpMid"].get<std::string>(),
            json_candidate["sdpMLineIndex"].get<int>(),
            json_candidate["candidate"].get<std::string>(),
        };
    }
};

class DummySetSessionDescriptionObserver : public ::webrtc::SetSessionDescriptionObserver {
  public:
    explicit DummySetSessionDescriptionObserver(std::string peer_id)
            : peer_id_(std::move(peer_id)) {}
    void OnSuccess() override {}
    void OnFailure(::webrtc::RTCError error) override {
        LOG(ERROR) << "WebRTC SetSessionDescription failed for participant " << peer_id_ << ": "
                   << error.message();
    }

  private:
    std::string peer_id_;
};

class DefaultSessionDescriptionObserver : public ::webrtc::CreateSessionDescriptionObserver {
  public:
    using ResultCallback = std::function<void(::webrtc::SessionDescriptionInterface*)>;

    explicit DefaultSessionDescriptionObserver(std::string peer_id, ResultCallback callback)
            : peer_id_(std::move(peer_id)), callback_(std::move(callback)) {}

    void OnSuccess(::webrtc::SessionDescriptionInterface* desc) override { callback_(desc); }
    void OnFailure(::webrtc::RTCError error) override {
        LOG(ERROR) << "WebRTC CreateSessionDescription failed for participant " << peer_id_ << ": "
                   << error.message();
    }

  private:
    std::string peer_id_;
    ResultCallback callback_;
};

Participant::Participant(RtcConnection& connection, std::string identity, nlohmann::json rtc_config,
                         std::shared_ptr<MediaProvider> media_provider)
        : connection_(connection)
        , peer_id_(std::move(identity))
        , rtc_config_(std::move(rtc_config))
        , media_provider_(std::move(media_provider)) {}

Participant::~Participant() {
    if (peer_connection_ != nullptr) {
        if (connection_.SignalingThread()->IsCurrent()) {
            DoClose();
        } else {
            LOG(DFATAL) << "Participant " << peer_id_
                        << " destroyed on non-signaling thread without being closed first! "
                        << "This will leak WebRTC resources.";
        }
    }
}

absl::Status Participant::Initialize() {
    if (connection_.SignalingThread()->IsCurrent()) {
        return DoInitialize();
    }
    absl::Status status;
    connection_.SignalingThread()->BlockingCall([&] { status = DoInitialize(); });
    return status;
}

absl::Status Participant::DoInitialize() {
    DCHECK(connection_.SignalingThread()->IsCurrent());
    if (auto s = CreatePeerConnection(rtc_config_); !s.ok()) {
        return s;
    }

    if (auto s = AddDataChannel(DataChannelLabel::kInput); !s.ok()) {
        return s;
    }

    if (media_provider_) {
        if (!media_provider_->AddTracks(connection_.GetPeerConnectionFactory(),
                                        peer_connection_.get())) {
            return absl::InternalError(absl::StrCat(
                    "Failed to add audio/video media tracks to PeerConnection for participant ",
                    peer_id_));
        }
    }
    return absl::OkStatus();
}

void Participant::IncomingMessage(const nlohmann::json& msg) {
    if (connection_.SignalingThread()->IsCurrent()) {
        DoIncomingMessage(msg);
    } else {
        connection_.SignalingThread()->BlockingCall([&] { DoIncomingMessage(msg); });
    }
}

void Participant::DoIncomingMessage(const nlohmann::json& msg) {
    DCHECK(connection_.SignalingThread()->IsCurrent());
    if (msg.contains("candidate")) {
        if (msg["candidate"].is_object() && msg["candidate"].contains("candidate")) {
            HandleCandidate(msg["candidate"]);
        } else {
            HandleCandidate(msg);
        }
    }
    if (msg.contains("sdp")) {
        if (msg["sdp"].is_object() && msg["sdp"].contains("sdp")) {
            HandleOffer(msg["sdp"]);
        } else {
            HandleOffer(msg);
        }
    }
}

void Participant::OnIceCandidate(const ::webrtc::IceCandidateInterface* candidate) {
    DCHECK(connection_.SignalingThread()->IsCurrent());
    std::string sdp;
    if (!candidate->ToString(&sdp)) {
        LOG(ERROR) << "Failed to serialize local WebRTC ICE candidate for participant " << peer_id_
                   << ". Ignoring candidate.";
        return;
    }

    SendMessage({{"candidate",
                  {{"sdpMid", candidate->sdp_mid()},
                   {"sdpMLineIndex", candidate->sdp_mline_index()},
                   {"candidate", sdp}}}});
}

void Participant::OnConnectionChange(
        ::webrtc::PeerConnectionInterface::PeerConnectionState new_state) {
    DCHECK(connection_.SignalingThread()->IsCurrent());
    using State = ::webrtc::PeerConnectionInterface::PeerConnectionState;
    if (new_state == State::kFailed || new_state == State::kClosed) {
        bool expected = false;
        if (closed_.compare_exchange_strong(expected, true)) {
            connection_.RtcConnectionClosed(peer_id_);
        }
    }
}

void Participant::OnIceConnectionChange(
        ::webrtc::PeerConnectionInterface::IceConnectionState new_state) {
    DCHECK(connection_.SignalingThread()->IsCurrent());
    VLOG(1) << "ICE Connection state changed for participant " << peer_id_
            << " to WebRTC state: " << static_cast<int>(new_state);
}

void Participant::OnDataChannel(webrtc::scoped_refptr<::webrtc::DataChannelInterface> channel) {
    DCHECK(connection_.SignalingThread()->IsCurrent());
    LOG(INFO) << "Registered incoming remote WebRTC data channel '" << channel->label()
              << "' for participant " << peer_id_;
    data_channels_[channel->label()] = channel;
}

void Participant::DoClose() {
    DCHECK(connection_.SignalingThread()->IsCurrent());
    const absl::MutexLock lock(&closed_mutex_);
    if (peer_connection_) {
        peer_connection_->Close();
    }
    peer_connection_ = nullptr;
    event_forwarders_.clear();
    data_channels_.clear();
    queued_candidates_.clear();
    remote_description_set_ = false;
}

void Participant::Close() {
    // Ensure DoClose is called on the signaling thread.
    if (connection_.SignalingThread()->IsCurrent()) {
        DoClose();
    } else {
        connection_.SignalingThread()->BlockingCall([&] { DoClose(); });
    }
}

void Participant::WaitForClose() {
    auto is_closed = [this]() { return peer_connection_ == nullptr; };
    const absl::MutexLock lock(&closed_mutex_);
    closed_mutex_.Await(absl::Condition(&is_closed));
}

void Participant::SendMessage(const nlohmann::json& msg) {
    VLOG(1) << "Sending JSEP message to " << peer_id_ << ": " << msg.dump();
    connection_.Send(peer_id_, msg);
}

void Participant::HandleCandidate(const nlohmann::json& msg) {
    DCHECK(connection_.SignalingThread()->IsCurrent());
    auto candidate_result = IceCandidate::FromJson(msg);
    if (!candidate_result.ok()) {
        LOG(WARNING) << "Received invalid ICE candidate from " << peer_id_ << ": "
                     << candidate_result.status();
        return;
    }
    const auto& ice_candidate = *candidate_result;
    ::webrtc::SdpParseError error;

    IceCandidatePtr candidate(::webrtc::CreateIceCandidate(
            ice_candidate.sdp_mid, ice_candidate.sdp_mline_index, ice_candidate.candidate, &error));
    if (!candidate) {
        LOG(WARNING) << "Failed to parse remote ICE candidate for participant " << peer_id_ << ": "
                     << error.description;
        return;
    }
    if (!remote_description_set_) {
        VLOG(1) << "Queuing remote ICE candidate for participant " << peer_id_
                << " because remote description is not yet set.";
        queued_candidates_.push_back(std::move(candidate));
        return;
    }

    std::weak_ptr<Participant> weak_self = shared_from_this();
    peer_connection_->AddIceCandidate(std::move(candidate), [weak_self](
                                                                    const ::webrtc::RTCError& err) {
        if (auto self = weak_self.lock()) {
            if (!err.ok()) {
                LOG(ERROR)
                        << "Failed to add remote ICE candidate to PeerConnection for participant "
                        << self->peer_id_ << ": " << err.message();
            }
        }
    });
}

absl::StatusOr<SessionDescriptionPtr> Participant::ParseSdpMessage(const nlohmann::json& msg) {
    DCHECK(connection_.SignalingThread()->IsCurrent());
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

void Participant::HandleOffer(const nlohmann::json& msg) {
    DCHECK(connection_.SignalingThread()->IsCurrent());
    auto parsed_sdp = ParseSdpMessage(msg);
    if (!parsed_sdp.ok()) {
        LOG(WARNING) << "Invalid SDP offer from " << peer_id_ << ": " << parsed_sdp.status();
        return;
    }

    auto session_description = std::move(*parsed_sdp);

    auto sdp_type = session_description->GetType();
    std::weak_ptr<Participant> weak_self = shared_from_this();
    peer_connection_->SetRemoteDescription(
            std::move(session_description),
            webrtc::scoped_refptr<::webrtc::SetRemoteDescriptionObserverInterface>(
                    new webrtc::RefCountedObject<SetRemoteDescriptionCallback>(weak_self)));
    if (sdp_type == webrtc::SdpType::kOffer) {
        peer_connection_->CreateAnswer(
                new webrtc::RefCountedObject<DefaultSessionDescriptionObserver>(
                        peer_id_,
                        [weak_self](auto desc) {
                            if (auto self = weak_self.lock()) {
                                self->ReceivedSessionDescription(desc);
                            }
                        }),
                {});
    }
}

absl::Status Participant::CreatePeerConnection(const nlohmann::json& rtc_configuration) {
    DCHECK(connection_.SignalingThread()->IsCurrent());
    ::webrtc::Environment env = ::webrtc::CreateEnvironment();

    // Set up a custom WebRTC PortAllocator using the RtcConnection's shared network manager
    // and socket factory. We configure it to allow gathering on all network interfaces
    // (including loopback interfaces by clearing the ignore mask) to ensure compatibility
    // with local emulator setups and virtual bridge networking.
    auto allocator = std::make_unique<::webrtc::BasicPortAllocator>(
            env, connection_.GetNetworkManager(), connection_.GetSocketFactory());
    allocator->SetNetworkIgnoreMask(0);

    uint32_t flags = allocator->flags();
    allocator->set_flags(flags | ::webrtc::PORTALLOCATOR_ENABLE_ANY_ADDRESS_PORTS);

    ::webrtc::PeerConnectionDependencies dependencies(this);
    dependencies.allocator = std::move(allocator);

    // Instantiate the WebRTC PeerConnection using the parsed configuration (TURN, ICE servers,
    // etc.)
    auto pc = connection_.GetPeerConnectionFactory()->CreatePeerConnectionOrError(
            RtcConfig::Parse(rtc_configuration), std::move(dependencies));
    if (!pc.ok()) {
        return absl::InternalError(
                absl::StrCat("Failed to construct PeerConnection for participant ", peer_id_, ": ",
                             pc.error().message(), ". Check WebRTC config."));
    }
    peer_connection_ = pc.MoveValue();
    if (peer_connection_.get() == nullptr) {
        return absl::InternalError(
                absl::StrCat("Created PeerConnection is null for participant ", peer_id_));
    }
    return absl::OkStatus();
}

absl::Status Participant::AddDataChannel(DataChannelLabel label) {
    DCHECK(connection_.SignalingThread()->IsCurrent());
    const std::string label_str = AsString(label);
    auto channel = peer_connection_->CreateDataChannelOrError(label_str, nullptr);
    if (!channel.ok()) {
        return absl::InternalError(absl::StrCat("Failed to create WebRTC data channel '", label_str,
                                                "' for participant ", peer_id_, ": ",
                                                channel.error().message()));
    }
    auto data_channel = channel.MoveValue();
    data_channels_[label_str] = data_channel;
    event_forwarders_[label] = std::make_unique<EventForwarder>(
            data_channel, label, connection_.CreateInputSender(label));
    return absl::OkStatus();
}

void Participant::ReceivedSessionDescription(::webrtc::SessionDescriptionInterface* desc) {
    DCHECK(connection_.SignalingThread()->IsCurrent());
    peer_connection_->SetLocalDescription(
            new webrtc::RefCountedObject<DummySetSessionDescriptionObserver>(peer_id_), desc);

    std::string sdp;
    desc->ToString(&sdp);

    SendMessage({{"type", desc->type()}, {"sdp", sdp}});
}

void Participant::OnRemoteDescriptionSet() {
    DCHECK(connection_.SignalingThread()->IsCurrent());
    remote_description_set_ = true;
    LOG(INFO) << "Remote description set successfully. Draining " << queued_candidates_.size()
              << " queued remote ICE candidates for participant " << peer_id_;
    for (auto& candidate : queued_candidates_) {
        peer_connection_->AddIceCandidate(std::move(candidate), [this](const ::webrtc::RTCError&
                                                                               err) {
            if (!err.ok()) {
                LOG(ERROR)
                        << "Failed to add remote ICE candidate to PeerConnection for participant "
                        << peer_id_ << ": " << err.message();
            }
        });
    }
    queued_candidates_.clear();
}

}  // namespace goldfish::videobridge
