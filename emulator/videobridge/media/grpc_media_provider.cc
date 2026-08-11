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
#include "grpc_media_provider.h"

// Disable compiler warnings for external third-party headers. We wrap these in localized
// pragma blocks rather than using target 'copts' so that thread-safety analysis remains
// active on our own local source files.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/scoped_refptr.h"
#pragma clang diagnostic pop

#include "absl/log/log.h"
#include "grpc_audio_source.h"
#include "grpc_video_source.h"

namespace goldfish::videobridge {

GrpcMediaProvider::GrpcMediaProvider(std::shared_ptr<EmulatorClient> client, uint32_t display_id,
                                     std::string shared_memory_path)
        : client_(std::move(client))
        , display_id_(display_id)
        , shared_memory_path_(std::move(shared_memory_path)) {}

absl::Status GrpcMediaProvider::AddTracks(::webrtc::PeerConnectionFactoryInterface* factory,
                                          ::webrtc::PeerConnectionInterface* peer_connection) {
    if (!factory || !peer_connection) {
        return absl::InvalidArgumentError(
                "Failed to add media tracks: PeerConnectionFactory or PeerConnection is null.");
    }

    // 1. Create and start GrpcVideoSource.
    GrpcVideoSourceOptions video_options;
    video_options.display_id = display_id_;
    video_options.width = 0;
    video_options.height = 0;
    if (!shared_memory_path_.empty()) {
        video_options.transport = GrpcVideoSourceOptions::Transport::kSharedMemory;
        video_options.shared_memory_path = std::filesystem::path(shared_memory_path_);
    } else {
        video_options.transport = GrpcVideoSourceOptions::Transport::kGrpcBytes;
    }

    auto video_source = ::webrtc::make_ref_counted<GrpcVideoSource>(client_, video_options);
    video_source->Start();

    // 2. Create VideoTrack wrapper.
    std::string video_track_id = absl::StrCat("video_track_id_", display_id_);
    std::string video_stream_id = absl::StrCat("emulator_stream_", display_id_);
    auto video_track = factory->CreateVideoTrack(video_source, video_track_id);
    if (!video_track) {
        video_source->Stop();
        return absl::InternalError("Failed to create WebRTC VideoTrack from GrpcVideoSource.");
    }

    // 3. Add VideoTrack to PeerConnection.
    auto video_result = peer_connection->AddTrack(video_track, {video_stream_id});
    if (!video_result.ok()) {
        video_source->Stop();
        return absl::InternalError(absl::StrCat("Failed to add VideoTrack to PeerConnection: ",
                                                video_result.error().message()));
    }

    // 4. Create and start GrpcAudioSource.
    auto audio_source = ::webrtc::make_ref_counted<GrpcAudioSource>(client_);
    audio_source->Start();

    // 5. Create AudioTrack wrapper.
    auto audio_track = factory->CreateAudioTrack("audio_track", audio_source.get());
    if (!audio_track) {
        audio_source->Stop();
        return absl::InternalError("Failed to create WebRTC AudioTrack from GrpcAudioSource.");
    }

    // 6. Add AudioTrack to PeerConnection.
    auto audio_result = peer_connection->AddTrack(audio_track, {"emulator_stream"});
    if (!audio_result.ok()) {
        audio_source->Stop();
        return absl::InternalError(absl::StrCat("Failed to add AudioTrack to PeerConnection: ",
                                                audio_result.error().message()));
    }

    LOG(INFO) << "Successfully added GrpcVideoSource and GrpcAudioSource tracks to PeerConnection.";
    return absl::OkStatus();
}

}  // namespace goldfish::videobridge
