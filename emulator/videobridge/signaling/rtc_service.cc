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
#include "goldfish/videobridge/rtc_service.h"

#include <chrono>
#include <thread>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"

#include "android/emulation/control/absl_status_translate.h"
#include "nlohmann/json.hpp"

namespace goldfish::videobridge {

namespace v2 = ::android::emulation::control::v2;

namespace {

std::string GenerateGuid() {
    // TODO(jansene): use thread_local if this is used for many concurrent connections.
    absl::BitGen bitgen;
    return absl::StrFormat("%08x-%04x-%04x-%04x-%12x", absl::Uniform<uint32_t>(bitgen),
                           absl::Uniform<uint16_t>(bitgen), absl::Uniform<uint16_t>(bitgen),
                           absl::Uniform<uint16_t>(bitgen),
                           absl::Uniform<uint64_t>(bitgen) & 0xFFFFFFFFFFFFULL);
}

/**
 * @brief Converts a gRPC IceServerConfig protobuf into a JSON string conforming
 * to the standard WebRTC `RTCConfiguration` dictionary format.
 *
 * The generated JSON is consumed directly by JavaScript WebRTC clients (e.g.,
 * `new RTCPeerConnection(config)`).
 *
 * Example Output JSON:
 * @code
 * {
 *   "iceServers": [
 *     {
 *       "urls": ["stun:stun.l.google.com:19302"]
 *     },
 *     {
 *       "urls": ["turn:my-turn-server.com:3478"],
 *       "username": "user123",
 *       "credential": "secretpassword"
 *     }
 *   ],
 *   "iceTransportPolicy": "relay"
 * }
 * @endcode
 */
std::string IceServerConfigToJson(const v2::IceServerConfig& config) {
    nlohmann::json servers = nlohmann::json::array();
    for (const auto& server : config.ice_servers()) {
        nlohmann::json entry;
        entry["urls"] = std::vector<std::string>(server.urls().begin(), server.urls().end());
        if (!server.username().empty()) {
            entry["username"] = server.username();
        }
        if (!server.credential().empty()) {
            entry["credential"] = server.credential();
        }
        servers.push_back(std::move(entry));
    }

    nlohmann::json rtc_config;
    rtc_config["iceServers"] = std::move(servers);
    if (!config.ice_transport_policy().empty()) {
        rtc_config["iceTransportPolicy"] = config.ice_transport_policy();
    }
    return rtc_config.dump();
}

}  // namespace

RtcService::RtcService(std::shared_ptr<Switchboard> switchboard)
        : switchboard_(std::move(switchboard)) {
    DCHECK(switchboard_ != nullptr) << "Switchboard reference cannot be null.";
}

::grpc::Status RtcService::RequestRtcStream(::grpc::ServerContext* /*context*/,
                                            const v2::RtcStreamRequest* request,
                                            v2::RtcStreamResponse* response) {
    const std::string guid = GenerateGuid();
    std::string turn_config;
    if (request->has_ice_server_config()) {
        turn_config = IceServerConfigToJson(request->ice_server_config());
    }

    LOG(INFO) << "Initializing WebRTC stream request for participant connection session '" << guid
              << "'";
    if (!switchboard_->Connect(guid, turn_config)) {
        LOG(ERROR) << "Failed to register participant: '" << guid << "' in Switchboard.";
        return {::grpc::StatusCode::INTERNAL, "Failed to connect participant."};
    }

    response->mutable_id()->set_guid(guid);
    return ::grpc::Status::OK;
}

::grpc::Status RtcService::SendJsepMessage(::grpc::ServerContext* /*context*/,
                                           const v2::SendJsepMessageRequest* request,
                                           v2::SendJsepMessageResponse* /*response*/) {
    const std::string& guid = request->jsep_msg().id().guid();
    const std::string& message = request->jsep_msg().message();

    if (guid.empty() || message.empty()) {
        return {::grpc::StatusCode::INVALID_ARGUMENT,
                "Session ID or message payload cannot be empty."};
    }

    if (auto status = switchboard_->AcceptJsepMessage(guid, message); !status.ok()) {
        VLOG(1) << "SendJsepMessage: routing failed for session ID: '" << guid
                << "'. Payload was: " << message;
        return ::android::emulation::control::AbslStatusToGrpcStatus(status);
    }
    return ::grpc::Status::OK;
}

::grpc::Status RtcService::ReceiveJsepMessageStream(
        ::grpc::ServerContext* context, const v2::ReceiveJsepMessageRequest* request,
        ::grpc::ServerWriter<v2::ReceiveJsepMessageResponse>* writer) {
    const std::string& guid = request->id().guid();
    if (guid.empty()) {
        return {::grpc::StatusCode::INVALID_ARGUMENT, "Session ID cannot be empty."};
    }

    VLOG(1) << "Opening server-streaming signaling channel for session: '" << guid << "'";
    while (!context->IsCancelled()) {
        // Note: In the synchronous gRPC API, there is no callback hook to notify us
        // when a client cancels a stream. To prevent threads from blocking indefinitely
        // and leaking resources, we poll with a 500ms timeout. This allows us to
        // regularly check `context->IsCancelled()` and exit the loop.
        auto maybe_msg = switchboard_->NextMessage(guid, absl::Milliseconds(500));
        if (!maybe_msg.ok()) {
            if (absl::IsDeadlineExceeded(maybe_msg.status())) {
                continue;
            }
            VLOG(1) << "Signaling channel error: " << maybe_msg.status() << " for session: '"
                    << guid << "'";
            break;
        }

        v2::ReceiveJsepMessageResponse response;
        response.mutable_jsep_msg()->mutable_id()->set_guid(guid);
        response.mutable_jsep_msg()->set_message(*maybe_msg);
        if (!writer->Write(response)) {
            VLOG(1) << "Failed to write signaling message. Client may have disconnected for "
                       "session: '"
                    << guid << "'";
            break;
        }
    }
    return ::grpc::Status::OK;
}

::grpc::Status RtcService::ReceiveJsepMessage(::grpc::ServerContext* /*context*/,
                                              const v2::ReceiveJsepMessageRequest* request,
                                              v2::ReceiveJsepMessageResponse* response) {
    const std::string& guid = request->id().guid();
    if (guid.empty()) {
        return {::grpc::StatusCode::INVALID_ARGUMENT, "Session ID cannot be empty."};
    }

    // Wait for at most 5 seconds for a single message.
    auto maybe_msg = switchboard_->NextMessage(guid, absl::Seconds(5));
    if (!maybe_msg.ok()) {
        return ::android::emulation::control::AbslStatusToGrpcStatus(maybe_msg.status());
    }

    response->mutable_jsep_msg()->mutable_id()->set_guid(guid);
    response->mutable_jsep_msg()->set_message(*maybe_msg);
    return ::grpc::Status::OK;
}

}  // namespace goldfish::videobridge
