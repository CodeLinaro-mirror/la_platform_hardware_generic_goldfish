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

#include <cstdint>
#include <utility>

#include "absl/log/log.h"

#include "goldfish/audio/qemu_audio_capture.h"
#include "goldfish/videobridge/in_process_audio_source.h"

namespace goldfish::audio {

/**
 * WebRTC audio source capturing guest PCM audio directly from QEMU's AudioBackend.
 *
 * Concept & Data Flow:
 * - Couples InProcessAudioSource with QemuAudioCapture to bridge emulator audio into WebRTC.
 * - Reactive Activation: Audio capture automatically starts when the first WebRTC sink
 *   attaches and stops when all participants disconnect, eliminating background capture overhead.
 * - Raw 16-bit PCM audio samples are pushed from QEMU mixer/vCPU threads, buffered, and
 *   sliced into precise 10ms frames dispatched to active WebRTC audio sinks.
 *
 * Thread Safety:
 * - OnAudioData ingestion from QEMU audio threads is thread-safe and non-blocking.
 * - Start and Stop lifecycle methods are marshalled on WebRTC signaling threads.
 *
 * Ownership & Lifetime:
 * - Managed via webrtc::scoped_refptr (implements webrtc::AudioSourceInterface).
 * - Privately owns and manages the lifecycle of the underlying QemuAudioCapture instance.
 */
class QemuAudioSource : public ::goldfish::videobridge::InProcessAudioSource {
  public:
    /**
     * Constructs a QEMU WebRTC audio source.
     *
     * @param audio_backend Pointer to the QEMU AudioBackend, or nullptr for default backend.
     * @param sample_rate Ingestion and WebRTC streaming sample rate in Hz (default: 48000).
     * @param channels Number of audio channels (default: 2 for stereo).
     */
    explicit QemuAudioSource(AudioBackend* audio_backend = nullptr, uint32_t sample_rate = 48000,
                             uint32_t channels = 2);
    ~QemuAudioSource() override;

  protected:
    void OnStart() override;
    void OnStop() override;

  private:
    QemuAudioCapture capture_;
};

}  // namespace goldfish::audio
