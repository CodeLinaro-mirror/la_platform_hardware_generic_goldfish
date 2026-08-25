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

#include "goldfish/videobridge/media_track_provider.h"

#include <utility>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

namespace goldfish::videobridge {

MediaTrackProvider::MediaTrackProvider(
        ::webrtc::scoped_refptr<::webrtc::VideoTrackSourceInterface> video_source,
        ::webrtc::scoped_refptr<::webrtc::AudioSourceInterface> audio_source,
        std::string video_track_id, std::string video_stream_id, std::string audio_track_id,
        std::string audio_stream_id)
        : video_source_(std::move(video_source))
        , audio_source_(std::move(audio_source))
        , video_track_id_(std::move(video_track_id))
        , video_stream_id_(std::move(video_stream_id))
        , audio_track_id_(std::move(audio_track_id))
        , audio_stream_id_(std::move(audio_stream_id)) {}

absl::Status MediaTrackProvider::AddVideoTrack(::webrtc::PeerConnectionFactoryInterface* factory,
                                               ::webrtc::PeerConnectionInterface* peer_connection) {
    auto video_track = factory->CreateVideoTrack(video_source_, video_track_id_);
    if (!video_track) {
        return absl::InternalError(absl::StrCat("Failed to create WebRTC VideoTrack '",
                                                video_track_id_, "' from video source."));
    }

    auto video_result = peer_connection->AddTrack(video_track, {video_stream_id_});
    if (!video_result.ok()) {
        return absl::InternalError(absl::StrCat(
                "Failed to add VideoTrack '", video_track_id_, "' (stream: '", video_stream_id_,
                "') to PeerConnection: ", video_result.error().message()));
    }

    VLOG(1) << "MediaTrackProvider: Attached VideoTrack '" << video_track_id_ << "' (stream: '"
            << video_stream_id_ << "') to PeerConnection.";
    return absl::OkStatus();
}

absl::Status MediaTrackProvider::AddAudioTrack(::webrtc::PeerConnectionFactoryInterface* factory,
                                               ::webrtc::PeerConnectionInterface* peer_connection) {
    auto audio_track = factory->CreateAudioTrack(audio_track_id_, audio_source_.get());
    if (!audio_track) {
        return absl::InternalError(absl::StrCat("Failed to create WebRTC AudioTrack '",
                                                audio_track_id_, "' from audio source."));
    }

    auto audio_result = peer_connection->AddTrack(audio_track, {audio_stream_id_});
    if (!audio_result.ok()) {
        return absl::InternalError(absl::StrCat(
                "Failed to add AudioTrack '", audio_track_id_, "' (stream: '", audio_stream_id_,
                "') to PeerConnection: ", audio_result.error().message()));
    }

    VLOG(1) << "MediaTrackProvider: Attached AudioTrack '" << audio_track_id_ << "' (stream: '"
            << audio_stream_id_ << "') to PeerConnection.";
    return absl::OkStatus();
}

absl::Status MediaTrackProvider::AddTracks(::webrtc::PeerConnectionFactoryInterface* factory,
                                           ::webrtc::PeerConnectionInterface* peer_connection) {
    if (!factory) {
        return absl::InvalidArgumentError(
                "MediaTrackProvider::AddTracks failed: PeerConnectionFactory is null.");
    }
    if (!peer_connection) {
        return absl::InvalidArgumentError(
                "MediaTrackProvider::AddTracks failed: PeerConnection is null.");
    }

    if (!video_source_ && !audio_source_) {
        return absl::FailedPreconditionError(
                "MediaTrackProvider: Neither video source nor audio source was configured.");
    }

    if (video_source_) {
        if (auto status = AddVideoTrack(factory, peer_connection); !status.ok()) {
            return status;
        }
    }

    if (audio_source_) {
        if (auto status = AddAudioTrack(factory, peer_connection); !status.ok()) {
            return status;
        }
    }

    return absl::OkStatus();
}

}  // namespace goldfish::videobridge
