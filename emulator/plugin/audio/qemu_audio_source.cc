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

#include "goldfish/audio/qemu_audio_source.h"

#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"

namespace goldfish::audio {

QemuAudioSource::QemuAudioSource(AudioBackend* audio_backend, uint32_t sample_rate,
                                 uint32_t channels)
        : InProcessAudioSource(sample_rate, channels)
        , capture_(
                  [this](const int16_t* pcm_data, size_t num_samples) {
                      DCHECK(capture_.IsRunning()) << "Received PCM audio samples from QEMU after "
                                                      "audio capture was stopped.";
                      OnAudioData(pcm_data, num_samples);
                  },
                  static_cast<int>(sample_rate), static_cast<int>(channels), audio_backend) {}

QemuAudioSource::~QemuAudioSource() {
    // Synchronously unregisters capture under VM lock, guaranteeing no further callbacks arrive.
    capture_.Stop();
}

void QemuAudioSource::OnStart() {
    InProcessAudioSource::OnStart();
    if (auto status = capture_.Start(); !status.ok()) {
        LOG(WARNING) << "Failed to start QEMU audio capture: " << status
                     << "; WebRTC audio stream will produce silence.";
    }
}

void QemuAudioSource::OnStop() {
    // Synchronously unregisters capture under VM lock, guaranteeing no further callbacks arrive.
    capture_.Stop();
    InProcessAudioSource::OnStop();
}

}  // namespace goldfish::audio
