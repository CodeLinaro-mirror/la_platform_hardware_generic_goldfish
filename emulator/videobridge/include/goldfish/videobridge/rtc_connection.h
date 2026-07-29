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
#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#include "rtc_base/thread.h"
#pragma clang diagnostic pop

#include <memory>
#include <string>

#include "goldfish/videobridge/input_sender.h"
#include "nlohmann/json.hpp"
#include "rtc_base/network.h"

namespace webrtc {
class NetworkManager;
class PacketSocketFactory;
}  // namespace webrtc

namespace goldfish::videobridge {

/**
 * @class RtcConnection
 * @brief Base connection manager coordinating the WebRTC engine, threading models, and factory
 * instances.
 *
 * This class abstracts the foundational setup of WebRTC threads (worker, signaling, and network
 * threads) and holds the PeerConnectionFactoryInterface. It serves as the context for participant
 * PeerConnections.
 */
class RtcConnection {
  public:
    RtcConnection();
    virtual ~RtcConnection();

    /**
     * @brief Called when a participant's WebRTC connection changes to a terminal closed state.
     *
     * @param participant The unique identifier of the participant session that closed.
     */
    virtual void RtcConnectionClosed(std::string participant) = 0;

    /**
     * @brief Sends a signaling message back to the client side.
     *
     * @param to The target participant identifier.
     * @param msg The signaling payload in JSON format.
     */
    virtual void Send(std::string to, const nlohmann::json& msg) = 0;

    /**
     * @brief Exposes the current WebRTC PeerConnectionFactory interface.
     * @return Pointer to the active connection factory.
     */
    ::webrtc::PeerConnectionFactoryInterface* GetPeerConnectionFactory() {
        return connection_factory_.get();
    }

    /**
     * @brief Exposes the dedicated WebRTC signaling thread.
     * @return Pointer to the signaling thread instance.
     */
    webrtc::Thread* SignalingThread() { return signaling_thread_.get(); }

    /**
     * @brief Exposes the dedicated WebRTC network thread.
     * @return Pointer to the network thread instance.
     */
    webrtc::Thread* NetworkThread() { return network_thread_.get(); }

    /**
     * @brief Exposes the dedicated WebRTC worker thread.
     * @return Pointer to the worker thread instance.
     */
    webrtc::Thread* WorkerThread() { return worker_thread_.get(); }

    /**
     * @brief Factory method creating a concrete InputSender channel for the given data channel
     * type.
     *
     * @param label The category of the data channel.
     * @return std::unique_ptr<InputSender> A new InputSender instance.
     */
    virtual std::unique_ptr<InputSender> CreateInputSender(DataChannelLabel label) = 0;

    // Helper methods to satisfy Participant's need for a NetworkManager and SocketFactory
    // if using the connection's threads.
    ::webrtc::NetworkManager* GetNetworkManager() { return network_manager_.get(); }
    ::webrtc::PacketSocketFactory* GetSocketFactory() { return socket_factory_.get(); }

  protected:
    std::unique_ptr<::webrtc::TaskQueueFactory> task_factory_;
    std::unique_ptr<webrtc::Thread> network_thread_;
    std::unique_ptr<webrtc::Thread> worker_thread_;
    std::unique_ptr<webrtc::Thread> signaling_thread_;
    std::unique_ptr<::webrtc::NetworkManager> network_manager_;
    std::unique_ptr<::webrtc::PacketSocketFactory> socket_factory_;
    webrtc::scoped_refptr<::webrtc::PeerConnectionFactoryInterface> connection_factory_;
};

}  // namespace goldfish::videobridge
