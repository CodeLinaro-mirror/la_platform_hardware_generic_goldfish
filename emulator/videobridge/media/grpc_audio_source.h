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
#include "grpcpp/grpcpp.h"

#include "api/audio_options.h"
#include "api/media_stream_interface.h"
#pragma clang diagnostic pop

#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "absl/synchronization/mutex.h"

#include "goldfish/videobridge/emulator_client.h"

namespace goldfish::videobridge {

/**
 * @class GrpcAudioSource
 * @brief Custom AudioSourceInterface streaming emulator virtual microphone audio via gRPC.
 *
 * GrpcAudioSource subscribes to emulator audio output, slices variable-length incoming streams
 * into exact 10ms frames required by WebRTC, and pushes them to registered sinks.
 */
class GrpcAudioSource : public ::webrtc::AudioSourceInterface {
  public:
    explicit GrpcAudioSource(std::shared_ptr<EmulatorClient> client);
    ~GrpcAudioSource() override;

    // AudioSourceInterface overrides.
    void AddSink(::webrtc::AudioTrackSinkInterface* sink) override;
    void RemoveSink(::webrtc::AudioTrackSinkInterface* sink) override;

    ::webrtc::MediaSourceInterface::SourceState state() const override {
        return ::webrtc::MediaSourceInterface::SourceState::kLive;
    }
    void RegisterObserver(::webrtc::ObserverInterface* observer) override {}
    void UnregisterObserver(::webrtc::ObserverInterface* observer) override {}
    bool remote() const override { return false; }

    const ::webrtc::AudioOptions options() const override;

    /**
     * @brief Spins up the background capture thread.
     */
    void Start();

    /**
     * @brief Cancels the stream and joins the background capture thread.
     */
    void Stop();

  private:
    void CaptureLoop();
    void ConsumeAudioPacket(const AudioPacket& audio_packet);
    void DeliverFrame(const void* audio_data, int bits_per_sample, int sample_rate,
                      size_t number_of_channels, size_t number_of_frames);

    std::shared_ptr<EmulatorClient> client_;

    std::atomic<bool> running_{false};
    std::thread capture_thread_;
    ::grpc::ClientContext context_;

    absl::Mutex sinks_mutex_;
    std::set<::webrtc::AudioTrackSinkInterface*> sinks_ ABSL_GUARDED_BY(sinks_mutex_);

    std::vector<uint8_t> partial_frame_;
};

}  // namespace goldfish::videobridge
