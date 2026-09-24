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

#include <grpcpp/grpcpp.h>

#include <functional>
#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "goldfish/videobridge/switchboard.h"
#include "webrtc/rtc_service.grpc.pb.h"

namespace goldfish::grpc::v2 {

/**
 * @class RtcServiceImpl
 * @brief Implements AEMU v2 RtcService providing WebRTC signaling and stream orchestration.
 *
 * RtcServiceImpl coordinates the signaling lifecycle for incoming WebRTC sessions and maps
 * protobuf stream requests to the underlying Switchboard participant connections.
 *
 * It provides thread-safe concurrent session management, automatic garbage collection for
 * abandoned sessions, and maps internal absl::Status codes directly to canonical gRPC Status.
 */
class RtcServiceImpl : public ::android::emulation::v2::webrtc::RtcService::Service {
  public:
    using Switchboard = ::goldfish::videobridge::Switchboard;
    using Clock = std::function<absl::Time()>;

    /**
     * @brief How long a session created by RequestRtcStream may sit unclaimed before being swept.
     *
     * In production, the client attaches its signaling stream immediately upon receiving the
     * session id. If the client dies or drops the connection before attaching, the session
     * lingers until the next RequestRtcStream runs past this cutoff.
     */
    static constexpr absl::Duration kDefaultUnclaimedSessionTimeout = absl::Seconds(60);

    /**
     * @brief How long a signaling stream waits for a message before re-checking cancellation.
     *
     * This is not a latency budget. Switchboard wakes a blocked reader the moment a message
     * is queued or the session closes, so the interval only bounds how quickly the handler
     * notices that the *client* went away, which the synchronous gRPC API offers no callback
     * for. Matches the v1 signaling service.
     */
    static constexpr absl::Duration kJsepPollInterval = absl::Milliseconds(500);

    /**
     * @param switchboard The WebRTC coordinator. Must not be null.
     * @param unclaimed_session_timeout Overridable purely so tests need not wait a minute.
     * @param clock Overridable purely so tests can age sessions without sleeping.
     */
    explicit RtcServiceImpl(
            std::shared_ptr<Switchboard> switchboard,
            absl::Duration unclaimed_session_timeout = kDefaultUnclaimedSessionTimeout,
            Clock clock = absl::Now);
    ~RtcServiceImpl() override = default;

    ::grpc::Status RequestRtcStream(
            ::grpc::ServerContext* context,
            const ::android::emulation::v2::webrtc::RtcStreamRequest* request,
            ::android::emulation::v2::webrtc::RtcStreamResponse* response) override;

    ::grpc::Status UpdateRtcStream(
            ::grpc::ServerContext* context,
            const ::android::emulation::v2::webrtc::RtcStreamUpdateRequest* request,
            ::google::protobuf::Empty* response) override;

    ::grpc::Status SendJsepMessage(::grpc::ServerContext* context,
                                   const ::android::emulation::v2::webrtc::JsepMessage* request,
                                   ::google::protobuf::Empty* response) override;

    ::grpc::Status ReceiveJsepMessageStream(
            ::grpc::ServerContext* context,
            const ::android::emulation::v2::webrtc::RtcSession* request,
            ::grpc::ServerWriter<::android::emulation::v2::webrtc::JsepMessage>* writer) override;

  protected:
    /**
     * @brief Blocks until a signaling stream has attached to the given session.
     *
     * Used by tests to synchronize against stream establishment without sleeps.
     */
    void AwaitSessionAttached(const std::string& session_id) const;

    /**
     * @brief Blocks until a signaling stream has detached from the given session.
     *
     * Used by tests to synchronize against stream teardown without sleeps.
     */
    void AwaitSessionDetached(const std::string& session_id) const;

  private:
    /**
     * @brief Records a freshly created session as awaiting a signaling stream.
     */
    void TrackUnclaimedSession(const std::string& session_id);

    /**
     * @brief Atomically claims a session and reserves its single signaling stream slot.
     *
     * Erases the session from the unclaimed sweep map and records it in attached_sessions_.
     *
     * @return false if a signaling stream is already attached to this session.
     */
    bool TryAttachAndClaimSession(const std::string& session_id);

    /**
     * @brief Releases the signaling stream slot for a session.
     */
    void DetachSession(const std::string& session_id);

    /**
     * @brief Disconnects sessions that were created but never claimed within the timeout.
     *
     * Called from RequestRtcStream so that reclamation is driven by the same traffic that
     * creates sessions. This avoids a dedicated timer thread while still bounding how many
     * abandoned PeerConnections can accumulate.
     */
    void SweepUnclaimedSessions();

    const std::shared_ptr<Switchboard> switchboard_;
    const absl::Duration unclaimed_session_timeout_;
    const Clock clock_;

    mutable absl::Mutex sessions_mutex_;
    absl::flat_hash_map<std::string, absl::Time> unclaimed_sessions_
            ABSL_GUARDED_BY(sessions_mutex_);
    absl::flat_hash_set<std::string> attached_sessions_ ABSL_GUARDED_BY(sessions_mutex_);
};

}  // namespace goldfish::grpc::v2
