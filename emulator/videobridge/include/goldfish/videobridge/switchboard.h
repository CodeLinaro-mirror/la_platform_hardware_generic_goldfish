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

#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "goldfish/videobridge/media_provider.h"
#include "goldfish/videobridge/rtc_connection.h"
#include "nlohmann/json.hpp"

namespace goldfish::videobridge {

class Participant;
class EmulatorClient;

/**
 * @class Switchboard
 * @brief Thread-safe manager coordinating participant connection lifecycles, ICE settings, and
 * signal exchanges.
 *
 * Switchboard is the concrete implementation of RtcConnection. It maintains active Participant
 * sessions, handles signaling transport queue allocations, and coordinates callback-driven or
 * blocking signaling retrievals for gRPC service layers.
 */
class Switchboard : public RtcConnection {
  public:
    /**
     * @brief Constructs the Switchboard coordinator.
     *
     * @param client Pointer to the EmulatorClient gRPC channel.
     * @param media_provider Optional provider injected to set up audio/video streams for
     * participants.
     */
    explicit Switchboard(std::shared_ptr<EmulatorClient> client,
                         std::shared_ptr<MediaProvider> media_provider = nullptr);
    ~Switchboard() override;

    /**
     * @brief Establishes a new Participant connection session, initializing its PeerConnection.
     * Thread-safe.
     *
     * @param identity The unique identifier of the client.
     * @param turn_config Serialized JSON string containing TURN/STUN settings.
     * @return true if connection creation was successful.
     */
    bool Connect(const std::string& identity, const std::string& turn_config = "");

    /**
     * @brief Disconnects the participant, purges their signaling queues, and notifies closed
     * callbacks. Thread-safe.
     *
     * @param identity The client identifier to disconnect.
     */
    void Disconnect(const std::string& identity);

    /**
     * @brief Routes an incoming signaling packet (SDP or ICE) from the client to the participant's
     * PeerConnection. Thread-safe.
     *
     * @param identity The client identifier.
     * @param msg The raw JSEP signaling JSON payload.
     * @return absl::Status indicating routing and parsing success.
     */
    absl::Status AcceptJsepMessage(const std::string& identity, const std::string& msg);

    using MessageCallback = std::function<void(absl::StatusOr<std::string>)>;

    /**
     * @brief Synchronously blocks the calling thread waiting for the next outgoing JSEP message
     * from the participant. Used by unary gRPC or poll-based signaling transports.
     *
     * @param identity Client identifier.
     * @param timeout Maximum duration to block.
     * @return absl::StatusOr<std::string> The retrieved signaling payload, or an error status
     * (e.g., DeadlineExceeded).
     */
    absl::StatusOr<std::string> NextMessage(const std::string& identity, absl::Duration timeout);

    /**
     * @brief Asynchronously registers a callback to be executed once a new outgoing JSEP message is
     * available. Used by streaming gRPC signaling systems. Invokes callback instantly if a message
     * is already queued. Invokes callback with an error status if the participant disconnects.
     *
     * @param identity Client identifier.
     * @param callback The handler to execute when signaling data arrives.
     */
    void NextMessage(const std::string& identity, MessageCallback callback);

    // RtcConnection overrides
    /**
     * @brief Handles terminal closed state transitions for a participant.
     */
    void RtcConnectionClosed(std::string participant) override;

    /**
     * @brief Enqueues an outgoing WebRTC JSEP signaling JSON message from a participant.
     * Immediately dispatches message if an async callback is registered.
     */
    void Send(std::string to, const nlohmann::json& msg) override;

    /**
     * @brief Factory creating a GrpcInputSender stream for the data channel.
     */
    std::unique_ptr<InputSender> CreateInputSender(DataChannelLabel label) override;

  private:
    struct ParticipantQueue {
        absl::Mutex mutex;
        std::queue<std::string> queue;
        MessageCallback callback;

        ~ParticipantQueue() {
            if (callback) {
                callback(absl::CancelledError("Queue destroyed"));
            }
        }
    };

    std::shared_ptr<ParticipantQueue> GetOrCreateQueue(const std::string& identity);
    std::shared_ptr<ParticipantQueue> GetQueue(const std::string& identity);
    void RemoveQueue(const std::string& identity);

    std::shared_ptr<EmulatorClient> emulator_client_;
    std::shared_ptr<MediaProvider> media_provider_;

    absl::Mutex connections_mutex_;
    absl::flat_hash_map<std::string, std::shared_ptr<Participant>> connections_
            ABSL_GUARDED_BY(connections_mutex_);

    absl::Mutex queues_mutex_;
    absl::flat_hash_map<std::string, std::shared_ptr<ParticipantQueue>> message_queues_
            ABSL_GUARDED_BY(queues_mutex_);
};

}  // namespace goldfish::videobridge
