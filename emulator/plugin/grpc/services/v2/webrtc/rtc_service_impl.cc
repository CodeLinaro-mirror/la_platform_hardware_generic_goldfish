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

#include "webrtc/rtc_service_impl.h"

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "android/emulation/control/absl_status_translate.h"
#include "goldfish/videobridge/session_id.h"
#include "nlohmann/json.hpp"

namespace goldfish::grpc::v2 {

using ::android::emulation::v2::CloseRtcStreamRequest;
using ::android::emulation::v2::CloseRtcStreamResponse;
using ::android::emulation::v2::IceCandidate;
using ::android::emulation::v2::JsepMessage;
using ::android::emulation::v2::RtcSession;
using ::android::emulation::v2::RtcStreamRequest;
using ::android::emulation::v2::RtcStreamResponse;
using ::android::emulation::v2::RtcStreamUpdateRequest;
using ::android::emulation::v2::SendJsepMessageResponse;
using ::android::emulation::v2::SessionDescription;
using ::android::emulation::v2::UpdateRtcStreamResponse;
using ::goldfish::videobridge::GenerateSessionId;
using ::goldfish::videobridge::Switchboard;

namespace {

std::string SdpTypeToString(SessionDescription::Type type) {
    switch (type) {
    case SessionDescription::OFFER:
        return "offer";
    case SessionDescription::PRANSWER:
        return "pranswer";
    case SessionDescription::ANSWER:
        return "answer";
    case SessionDescription::ROLLBACK:
        return "rollback";
    default:
        return "";
    }
}

SessionDescription::Type StringToSdpType(std::string_view type) {
    if (type == "offer") return SessionDescription::OFFER;
    if (type == "pranswer") return SessionDescription::PRANSWER;
    if (type == "answer") return SessionDescription::ANSWER;
    if (type == "rollback") return SessionDescription::ROLLBACK;
    return SessionDescription::TYPE_UNSPECIFIED;
}

std::string SerializeJsepToSwitchboardJson(const JsepMessage& msg) {
    nlohmann::json payload;
    if (msg.has_sdp()) {
        payload["type"] = SdpTypeToString(msg.sdp().type());
        payload["sdp"] = msg.sdp().sdp();
    } else if (msg.has_ice_candidate()) {
        payload["candidate"] = msg.ice_candidate().candidate();
        payload["sdpMid"] = msg.ice_candidate().sdp_mid();
        payload["sdpMLineIndex"] = msg.ice_candidate().sdp_m_line_index();
        if (msg.ice_candidate().has_username_fragment()) {
            payload["usernameFragment"] = msg.ice_candidate().username_fragment();
        }
    }
    return payload.dump();
}

JsepMessage ParseSwitchboardJsonToJsep(std::string_view session_id, std::string_view raw_json) {
    JsepMessage response;
    response.mutable_handle()->set_session_id(session_id.data(), session_id.size());

    auto parsed = nlohmann::json::parse(raw_json, nullptr, false);
    if (!parsed.is_discarded() && parsed.is_object()) {
        if (parsed.contains("sdp") && parsed["sdp"].is_string()) {
            auto* sdp = response.mutable_sdp();
            sdp->set_sdp(parsed["sdp"].get<std::string>());
            if (parsed.contains("type") && parsed["type"].is_string()) {
                sdp->set_type(StringToSdpType(parsed["type"].get<std::string>()));
            }
            return response;
        }
        auto* ice = response.mutable_ice_candidate();
        if (parsed.contains("candidate") && parsed["candidate"].is_string()) {
            ice->set_candidate(parsed["candidate"].get<std::string>());
        }
        if (parsed.contains("sdpMid") && parsed["sdpMid"].is_string()) {
            ice->set_sdp_mid(parsed["sdpMid"].get<std::string>());
        }
        if (parsed.contains("sdpMLineIndex") && parsed["sdpMLineIndex"].is_number_integer()) {
            ice->set_sdp_m_line_index(parsed["sdpMLineIndex"].get<int32_t>());
        }
        if (parsed.contains("usernameFragment") && parsed["usernameFragment"].is_string()) {
            ice->set_username_fragment(parsed["usernameFragment"].get<std::string>());
        }
    }
    return response;
}

}  // namespace

RtcServiceImpl::RtcServiceImpl(std::shared_ptr<Switchboard> switchboard,
                               absl::Duration unclaimed_session_timeout, Clock clock)
        : switchboard_(std::move(switchboard))
        , unclaimed_session_timeout_(unclaimed_session_timeout)
        , clock_(std::move(clock)) {
    DCHECK(switchboard_ != nullptr) << "Switchboard reference cannot be null.";
}

::grpc::Status RtcServiceImpl::RequestRtcStream(::grpc::ServerContext* /*context*/,
                                                const RtcStreamRequest* request,
                                                RtcStreamResponse* response) {
    SweepUnclaimedSessions();

    const std::string session_id = GenerateSessionId();
    LOG(INFO) << "Initializing AEMU v2 WebRTC stream request for session '" << session_id << "'";

    // Media tracks are negotiated dynamically via standard JSEP SDP offer/answer.
    VLOG(1) << "RequestRtcStream: session '" << session_id
            << "' requested_tracks count: " << request->requested_tracks_size()
            << ", secondary_display_ids count: " << request->secondary_display_ids_size();

    // TODO(b/554875313): Populate ice_servers once server-side TURN credentials are supported.
    if (!switchboard_->Connect(session_id, "")) {
        LOG(ERROR) << "Failed to register participant: '" << session_id << "' in Switchboard.";
        return {::grpc::StatusCode::INTERNAL, "Failed to initialize WebRTC session."};
    }

    TrackUnclaimedSession(session_id);
    response->mutable_handle()->set_session_id(session_id);
    return ::grpc::Status::OK;
}

::grpc::Status RtcServiceImpl::UpdateRtcStream(::grpc::ServerContext* /*context*/,
                                               const RtcStreamUpdateRequest* request,
                                               UpdateRtcStreamResponse* /*response*/) {
    const std::string& session_id = request->handle().session_id();
    if (session_id.empty()) {
        return {::grpc::StatusCode::INVALID_ARGUMENT, "Session ID cannot be empty."};
    }
    if (!switchboard_->HasSession(session_id)) {
        return {::grpc::StatusCode::NOT_FOUND,
                "No active WebRTC session for the supplied session ID."};
    }

    // Dynamic track updates (adding or removing video/audio tracks during an active session)
    // are not currently supported by the underlying Switchboard implementation.
    if (request->added_tracks_size() > 0 || request->removed_tracks_size() > 0) {
        return {::grpc::StatusCode::UNIMPLEMENTED,
                "Dynamic track updates (added_tracks / removed_tracks) are not currently supported "
                "by the WebRTC backend."};
    }

    return ::grpc::Status::OK;
}

::grpc::Status RtcServiceImpl::CloseRtcStream(::grpc::ServerContext* /*context*/,
                                              const CloseRtcStreamRequest* request,
                                              CloseRtcStreamResponse* /*response*/) {
    const std::string& session_id = request->handle().session_id();
    if (session_id.empty()) {
        return {::grpc::StatusCode::INVALID_ARGUMENT, "Session ID cannot be empty."};
    }
    if (!switchboard_->HasSession(session_id)) {
        return {::grpc::StatusCode::NOT_FOUND,
                "No active WebRTC session for the supplied session ID."};
    }

    switchboard_->Disconnect(session_id);
    DetachSession(session_id);
    return ::grpc::Status::OK;
}

::grpc::Status RtcServiceImpl::SendJsepMessage(::grpc::ServerContext* /*context*/,
                                               const JsepMessage* request,
                                               SendJsepMessageResponse* /*response*/) {
    const std::string& session_id = request->handle().session_id();
    if (session_id.empty() || request->payload_case() == JsepMessage::PAYLOAD_NOT_SET) {
        return {::grpc::StatusCode::INVALID_ARGUMENT,
                "Session ID and JSEP payload (sdp or ice_candidate) cannot be empty."};
    }

    const std::string serialized = SerializeJsepToSwitchboardJson(*request);
    if (auto status = switchboard_->AcceptJsepMessage(session_id, serialized); !status.ok()) {
        VLOG(1) << "SendJsepMessage: routing failed for session '" << session_id << "': " << status;
        return ::android::emulation::control::AbslStatusToGrpcStatus(status);
    }
    return ::grpc::Status::OK;
}

::grpc::Status RtcServiceImpl::ReceiveJsepMessageStream(::grpc::ServerContext* context,
                                                        const RtcSession* request,
                                                        ::grpc::ServerWriter<JsepMessage>* writer) {
    const std::string& session_id = request->session_id();
    if (session_id.empty()) {
        return {::grpc::StatusCode::INVALID_ARGUMENT, "Session ID cannot be empty."};
    }
    if (!switchboard_->HasSession(session_id)) {
        return {::grpc::StatusCode::NOT_FOUND,
                "No active WebRTC session for the supplied session ID."};
    }
    if (!TryAttachAndClaimSession(session_id)) {
        return {::grpc::StatusCode::ALREADY_EXISTS,
                "A signaling stream is already attached to this session."};
    }
    // Canceling or dropping the signaling stream detaches the stream slot without
    // tearing down the underlying PeerConnection so clients can reconnect.
    const absl::Cleanup cleanup = [this, &session_id] { DetachSession(session_id); };

    while (!context->IsCancelled()) {
        absl::StatusOr<std::string> msg = switchboard_->NextMessage(session_id, kJsepPollInterval);
        if (absl::IsDeadlineExceeded(msg.status())) {
            continue;
        }
        if (!msg.ok()) {
            if (absl::IsCancelled(msg.status()) || absl::IsNotFound(msg.status())) {
                break;
            }
            return ::android::emulation::control::AbslStatusToGrpcStatus(msg.status());
        }

        if (!writer->Write(ParseSwitchboardJsonToJsep(session_id, *msg))) {
            VLOG(1) << "Signaling write failed; client left session '" << session_id << "'";
            break;
        }
    }

    return ::grpc::Status::OK;
}

void RtcServiceImpl::TrackUnclaimedSession(const std::string& session_id) {
    absl::MutexLock lock(&sessions_mutex_);
    unclaimed_sessions_[session_id] = clock_();
}

bool RtcServiceImpl::TryAttachAndClaimSession(const std::string& session_id) {
    absl::MutexLock lock(&sessions_mutex_);
    if (!attached_sessions_.insert(session_id).second) {
        return false;
    }
    unclaimed_sessions_.erase(session_id);
    return true;
}

void RtcServiceImpl::DetachSession(const std::string& session_id) {
    absl::MutexLock lock(&sessions_mutex_);
    attached_sessions_.erase(session_id);
}

void RtcServiceImpl::AwaitSessionAttached(const std::string& session_id) const {
    const auto attached = [this, &session_id]() ABSL_SHARED_LOCKS_REQUIRED(sessions_mutex_) {
        return attached_sessions_.contains(session_id);
    };
    absl::MutexLock lock(&sessions_mutex_);
    sessions_mutex_.Await(absl::Condition(&attached));
}

void RtcServiceImpl::AwaitSessionDetached(const std::string& session_id) const {
    const auto detached = [this, &session_id]() ABSL_SHARED_LOCKS_REQUIRED(sessions_mutex_) {
        return !attached_sessions_.contains(session_id);
    };
    absl::MutexLock lock(&sessions_mutex_);
    sessions_mutex_.Await(absl::Condition(&detached));
}

void RtcServiceImpl::SweepUnclaimedSessions() {
    std::vector<std::string> expired;
    {
        absl::MutexLock lock(&sessions_mutex_);
        const absl::Time cutoff = clock_() - unclaimed_session_timeout_;
        for (auto it = unclaimed_sessions_.begin(); it != unclaimed_sessions_.end();) {
            if (it->second < cutoff) {
                expired.push_back(it->first);
                unclaimed_sessions_.erase(it++);
            } else {
                ++it;
            }
        }
    }

    for (const std::string& session_id : expired) {
        LOG(INFO) << "Reclaiming WebRTC session '" << session_id
                  << "' that was never attached to a signaling stream.";
        switchboard_->Disconnect(session_id);
    }
}

}  // namespace goldfish::grpc::v2
