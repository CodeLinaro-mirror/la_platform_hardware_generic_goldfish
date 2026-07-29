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

#include <memory>

#include "goldfish/videobridge/emulator_client.h"
#include "goldfish/videobridge/media_provider.h"

namespace goldfish::videobridge {

/**
 * @class GrpcMediaProvider
 * @brief Concrete MediaProvider implementing audio and video capture pipelines over gRPC.
 *
 * GrpcMediaProvider is injected into Switchboard. When a new PeerConnection session is initialized,
 * GrpcMediaProvider constructs custom gRPC-fed Audio and Video track sources, starts their capture
 * routines, and adds them to the PeerConnection.
 */
class GrpcMediaProvider : public MediaProvider {
  public:
    explicit GrpcMediaProvider(std::shared_ptr<EmulatorClient> client, uint32_t display_id = 0,
                               std::string shared_memory_path = "");
    ~GrpcMediaProvider() override = default;

    /**
     * @brief Creates and registers gRPC-based audio and video tracks with the PeerConnection.
     * Starts background frame delivery threads.
     *
     * @param factory WebRTC PeerConnectionFactory used to build the tracks.
     * @param peer_connection Target PeerConnection.
     * @return true if both tracks were successfully created and registered.
     */
    bool AddTracks(::webrtc::PeerConnectionFactoryInterface* factory,
                   ::webrtc::PeerConnectionInterface* peer_connection) override;

  private:
    std::shared_ptr<EmulatorClient> client_;
    uint32_t display_id_;
    std::string shared_memory_path_;
};

}  // namespace goldfish::videobridge
