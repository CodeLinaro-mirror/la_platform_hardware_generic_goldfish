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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#pragma clang diagnostic pop

#include <string>

#include "goldfish/videobridge/media_provider.h"

namespace goldfish::videobridge {

/**
 * Attaches WebRTC video and audio tracks to active PeerConnections.
 *
 * Wraps pre-existing WebRTC video and audio sources and provisions corresponding
 * tracks on new participant sessions.
 *
 * Thread Safety:
 * - Instances are immutable after construction and can be safely shared across threads.
 * - AddTracks() must be called on the WebRTC signaling thread.
 *
 * Lifetime:
 * - Retains ref-counted references (scoped_refptr) to the injected video and audio sources.
 */
class MediaTrackProvider : public MediaProvider {
  public:
    /**
     * Constructs a track provider wrapping optional video and audio sources.
     *
     * Ingests scoped_refptr handles to video and audio sources. If any track ID or stream ID
     * is omitted, standard WebRTC default identifiers ("video_track", "audio_track",
     * "emulator_stream") are applied.
     */
    explicit MediaTrackProvider(
            ::webrtc::scoped_refptr<::webrtc::VideoTrackSourceInterface> video_source = nullptr,
            ::webrtc::scoped_refptr<::webrtc::AudioSourceInterface> audio_source = nullptr,
            std::string video_track_id = "video_track",
            std::string video_stream_id = "emulator_stream",
            std::string audio_track_id = "audio_track",
            std::string audio_stream_id = "emulator_stream");

    ~MediaTrackProvider() override = default;

    /**
     * Provisions and attaches video and/or audio tracks to the target PeerConnection.
     *
     * Must be called on the WebRTC signaling thread.
     *
     * Status Codes:
     * - OK: Configured tracks were successfully provisioned and attached.
     * - INVALID_ARGUMENT: `factory` or `peer_connection` is null.
     * - FAILED_PRECONDITION: Neither video_source nor audio_source was configured.
     * - INTERNAL: WebRTC track creation or peer attachment failed.
     */
    absl::Status AddTracks(::webrtc::PeerConnectionFactoryInterface* factory,
                           ::webrtc::PeerConnectionInterface* peer_connection) override;

    ::webrtc::scoped_refptr<::webrtc::VideoTrackSourceInterface> video_source() const {
        return video_source_;
    }
    ::webrtc::scoped_refptr<::webrtc::AudioSourceInterface> audio_source() const {
        return audio_source_;
    }

  private:
    absl::Status AddVideoTrack(::webrtc::PeerConnectionFactoryInterface* factory,
                               ::webrtc::PeerConnectionInterface* peer_connection);
    absl::Status AddAudioTrack(::webrtc::PeerConnectionFactoryInterface* factory,
                               ::webrtc::PeerConnectionInterface* peer_connection);

    ::webrtc::scoped_refptr<::webrtc::VideoTrackSourceInterface> video_source_;
    ::webrtc::scoped_refptr<::webrtc::AudioSourceInterface> audio_source_;
    std::string video_track_id_;
    std::string video_stream_id_;
    std::string audio_track_id_;
    std::string audio_stream_id_;
};

}  // namespace goldfish::videobridge
