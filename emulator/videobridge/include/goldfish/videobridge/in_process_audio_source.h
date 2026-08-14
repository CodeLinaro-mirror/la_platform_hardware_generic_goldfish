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
#include "api/audio_options.h"
#include "api/media_stream_interface.h"
#pragma clang diagnostic pop

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/socket_buffer.h"

namespace goldfish::videobridge {

/**
 * @class InProcessAudioSource
 * @brief Custom WebRTC AudioSourceInterface that captures audio directly in-process.
 *
 * InProcessAudioSource serves as an upstream audio provider in the WebRTC pipeline,
 * ingesting raw PCM audio streams (e.g., from the QEMU mixer or in-memory audio callbacks)
 * and delivering them to registered WebRTC audio sinks.
 *
 * ### WebRTC Lifecycle Contract:
 * - **Sink Management**: Implements `webrtc::AudioSourceInterface::AddSink` and
 *   `webrtc::AudioSourceInterface::RemoveSink`. WebRTC audio tracks (`webrtc::AudioTrackInterface`)
 *   and peer connection audio pipelines register and unregister their sinks dynamically.
 *   Registered sinks are non-owning raw pointers managed by the WebRTC track layer.
 * - **Lifecycle & State (`Start()` / `Stop()`)**: `Start()` and `Stop()` control the active
 *   capture and dispatch of audio frames (e.g., pausing during VM state changes or muting).
 *   Sinks remain registered across `Start()`/`Stop()` cycles so the media stream topology is
 *   preserved. `Stop()` clears internal ring buffers to prevent audio crackles or stale frames
 *   upon resuming.
 * - **10ms Frame Slicing**: WebRTC audio processing and encoders (such as Opus) expect audio
 *   delivered in standard 10ms increments. `InProcessAudioSource` buffers variable-sized incoming
 *   PCM data via a `goldfish::SocketBuffer` ring buffer, chunks it into exact 10ms frames
 *   (`sample_rate / 100` samples/channel), and dispatches them via
 * `AudioTrackSinkInterface::OnData`.
 */
class InProcessAudioSource : public ::webrtc::AudioSourceInterface {
  public:
    explicit InProcessAudioSource(uint32_t sample_rate = 44100, uint32_t channels = 2);
    ~InProcessAudioSource() override;

    // AudioSourceInterface overrides.
    /**
     * @brief Registers an audio sink to receive 10ms audio frames.
     *
     * Called by WebRTC audio tracks or media pipelines. Sinks are non-owning and must
     * remain valid until unregistered via RemoveSink().
     *
     * @param sink Pointer to the AudioTrackSinkInterface to add.
     */
    void AddSink(::webrtc::AudioTrackSinkInterface* sink) override;

    /**
     * @brief Unregisters a previously added audio sink.
     *
     * @param sink Pointer to the AudioTrackSinkInterface to remove.
     */
    void RemoveSink(::webrtc::AudioTrackSinkInterface* sink) override;

    bool remote() const override { return false; }

    const ::webrtc::AudioOptions options() const override;

    ::webrtc::MediaSourceInterface::SourceState state() const override {
        return ::webrtc::MediaSourceInterface::SourceState::kLive;
    }
    void RegisterObserver(::webrtc::ObserverInterface* /*observer*/) override {}
    void UnregisterObserver(::webrtc::ObserverInterface* /*observer*/) override {}

    /**
     * @brief Pushes incoming raw PCM audio data (16-bit signed integer format).
     *
     * Ingests variable-sized audio chunks, buffers them, slices them into 10ms frames,
     * and dispatches each 10ms frame to all registered sinks. If the source is stopped,
     * incoming data is dropped.
     *
     * @param pcm_data Pointer to interleaved 16-bit signed PCM samples.
     * @param num_samples Total number of samples across all channels.
     */
    void OnAudioData(const int16_t* pcm_data, size_t num_samples);

    /**
     * @brief Starts audio capture and frame dispatching to registered sinks.
     */
    void Start();

    /**
     * @brief Stops audio capture and flushes pending buffered frames.
     *
     * Drops incoming audio data and clears internal ring buffers to prevent stale frames
     * and audio crackles when capture is resumed. Registered sinks are preserved.
     */
    void Stop();

  private:
    void Dispatch10msFrame(const int16_t* frame_data, size_t samples_per_channel);

    const uint32_t sample_rate_;
    const uint32_t channels_;
    const size_t samples_per_10ms_channel_;
    const size_t samples_per_10ms_total_;

    mutable absl::Mutex sink_mutex_;
    absl::flat_hash_set<::webrtc::AudioTrackSinkInterface*> sinks_ ABSL_GUARDED_BY(sink_mutex_);

    std::atomic<bool> running_{false};
    absl::Mutex buffer_mutex_;
    goldfish::SocketBuffer audio_buffer_ ABSL_GUARDED_BY(buffer_mutex_);
    std::vector<int16_t> temp_frame_buffer_ ABSL_GUARDED_BY(buffer_mutex_);
};

}  // namespace goldfish::videobridge
