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
#pragma clang diagnostic pop

#include <atomic>
#include <cstdint>
#include <vector>

#include "absl/synchronization/mutex.h"

#include "goldfish/socket_buffer.h"
#include "goldfish/videobridge/managed_audio_track_source.h"

namespace goldfish::videobridge {

/**
 * WebRTC AudioSourceInterface capturing PCM audio directly in-process.
 *
 * Buffers variable-sized incoming PCM chunks into exact 10ms frames required by WebRTC,
 * automatically activating when active sinks are attached.
 *
 * Thread Safety:
 * - OnAudioData may be called from audio mixer worker threads.
 * - Sinks and frame dispatches are thread-safe.
 */
class InProcessAudioSource : public ManagedAudioTrackSource {
  public:
    explicit InProcessAudioSource(uint32_t sample_rate = 44100, uint32_t channels = 2);
    ~InProcessAudioSource() override;

    uint32_t sample_rate() const { return sample_rate_; }
    uint32_t channels() const { return channels_; }

    /**
     * Pushes incoming raw PCM audio data (16-bit signed integer format).
     *
     * Ingests variable-sized chunks, slices them into 10ms frames, and dispatches to sinks.
     */
    void OnAudioData(const int16_t* pcm_data, size_t num_samples);

  protected:
    void OnStart() override;
    void OnStop() override;

  private:
    const uint32_t sample_rate_;
    const uint32_t channels_;
    const size_t samples_per_10ms_channel_;
    const size_t samples_per_10ms_total_;

    std::atomic<bool> running_{false};
    absl::Mutex buffer_mutex_;
    goldfish::SocketBuffer audio_buffer_ ABSL_GUARDED_BY(buffer_mutex_);
    std::vector<int16_t> temp_frame_buffer_ ABSL_GUARDED_BY(buffer_mutex_);
};

}  // namespace goldfish::videobridge
