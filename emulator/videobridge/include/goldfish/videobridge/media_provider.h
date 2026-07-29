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
#pragma clang diagnostic pop

namespace goldfish::videobridge {

/**
 * @class MediaProvider
 * @brief Dependency injection interface for constructing and injecting WebRTC media tracks.
 *
 * Concrete implementations of this class define what audio and video tracks are
 * registered with active participant PeerConnections during initialization (e.g.
 * screen capturing streams, virtual microphones, or test media mocks).
 */
class MediaProvider {
  public:
    virtual ~MediaProvider() = default;

    /**
     * @brief Creates and registers audio and/or video tracks with the PeerConnection.
     * Called automatically during Participant::Initialize() after connection setup.
     *
     * @param factory The WebRTC PeerConnectionFactory used to build the tracks.
     * @param peer_connection The target PeerConnection to add the tracks to.
     * @return true if all tracks were successfully created and added, false otherwise.
     */
    virtual bool AddTracks(::webrtc::PeerConnectionFactoryInterface* factory,
                           ::webrtc::PeerConnectionInterface* peer_connection) = 0;
};

}  // namespace goldfish::videobridge
