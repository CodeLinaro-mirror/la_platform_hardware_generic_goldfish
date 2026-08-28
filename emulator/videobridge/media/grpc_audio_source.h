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
#include "goldfish/videobridge/managed_audio_track_source.h"

namespace goldfish::videobridge {

/**
 * WebRTC AudioSourceInterface streaming emulator audio output via gRPC.
 *
 * Automatically connects to emulator audio streaming when WebRTC sinks are active.
 */
class GrpcAudioSource : public ManagedAudioTrackSource {
  public:
    explicit GrpcAudioSource(std::shared_ptr<EmulatorClient> client);
    ~GrpcAudioSource() override;

  protected:
    void OnStart() override;
    void OnStop() override;

  private:
    void CaptureLoop();
    void ConsumeAudioPacket(const AudioPacket& audio_packet);

    std::shared_ptr<EmulatorClient> client_;

    std::atomic<bool> capture_running_{false};
    std::thread capture_thread_;
    std::unique_ptr<::grpc::ClientContext> context_;

    std::vector<uint8_t> partial_frame_;
};

}  // namespace goldfish::videobridge
