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

#include <cstddef>
#include <cstdint>
#include <memory>

#include "android/emulation/control/simple_async_grpc.h"
#include "emulator_controller.grpc.pb.h"

extern "C" {
typedef struct AudioBackend AudioBackend;
}

namespace goldfish::audio {
class QemuAudioCapture;
}

namespace android::emulation::control {

class AudioStreamWriter : public SimpleServerWriter<AudioPacket> {
  public:
    // Maximum pending packets buffered before dropping incoming audio (tail-drop).
    // With QEMU's 10-20ms audio mixing period, 50 packets provide a ~500ms to 1.0s jitter
    // buffer to absorb transient network latency while capping memory usage and drift.
    static constexpr size_t kMaxQueuedPackets = 50;

    explicit AudioStreamWriter(const AudioFormat& request, AudioBackend* audio_backend = nullptr);

    void OnDone() override;
    void OnCancel() override;

  private:
    // Invoked synchronously on QEMU's audio/vCPU thread from QemuAudioCapture.
    // Constructs an AudioPacket and enqueues it via WithSimpleQueueWriter::Write()
    // without performing blocking I/O on the vCPU thread.
    void OnAudioData(const int16_t* pcm_data, size_t num_samples);

    AudioFormat format_;
    std::unique_ptr<::goldfish::audio::QemuAudioCapture> capture_;
};

}  // namespace android::emulation::control
