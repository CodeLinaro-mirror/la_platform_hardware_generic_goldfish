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

#include "audio_stream_writer.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

#include "android/emulation/control/absl_status_translate.h"
#include "goldfish/audio/qemu_audio_capture.h"

namespace android::emulation::control {

AudioStreamWriter::AudioStreamWriter(const AudioFormat& request, AudioBackend* audio_backend) {
    if (!AudioFormat::Channels_IsValid(request.channels())) {
        std::string error_msg = absl::StrFormat(
                "Invalid channels value %d in AudioFormat. Supported options are "
                "AudioFormat::Mono (0) or AudioFormat::Stereo (1).",
                request.channels());
        LOG(WARNING) << "streamAudio: " << error_msg;
        Finish(grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, error_msg));
        return;
    }

    if (request.format() != AudioFormat::AUD_FMT_S16) {
        LOG(WARNING) << "streamAudio: Requested format "
                     << AudioFormat::SampleFormat_Name(request.format()) << " (" << request.format()
                     << ") is not supported. The emulator audio pipeline only "
                     << "produces AUD_FMT_S16 (16-bit signed PCM); streaming will proceed using "
                        "AUD_FMT_S16.";
    }

    uint32_t sample_rate =
            request.samplingrate() > 0 ? static_cast<uint32_t>(request.samplingrate()) : 44100;
    uint32_t channels = (request.channels() == AudioFormat::Mono) ? 1 : 2;

    format_ = request;
    format_.set_samplingrate(sample_rate);
    format_.set_channels(request.channels());
    format_.set_format(AudioFormat::AUD_FMT_S16);

    capture_ = std::make_unique<::goldfish::audio::QemuAudioCapture>(
            [this](const int16_t* pcm_data, size_t num_samples) {
                OnAudioData(pcm_data, num_samples);
            },
            sample_rate, channels, audio_backend);

    if (auto status = capture_->Start(); !status.ok()) {
        std::string error_msg = absl::StrFormat(
                "Failed to start guest audio capture: %s. Ensure the emulator was started with "
                "audio enabled (e.g. without -no-audio).",
                status.message());
        LOG(WARNING) << "streamAudio: " << error_msg << " Closing stream.";
        Finish(grpc::Status(AbslStatusToGrpcStatus(status).error_code(), error_msg));
    }
}

void AudioStreamWriter::OnAudioData(const int16_t* pcm_data, size_t num_samples) {
    // Note: OnAudioData is called directly from QEMU's audio/vCPU thread.

    if (QueueSize() >= kMaxQueuedPackets) {
        LOG_EVERY_N_SEC(WARNING, 5)
                << "streamAudio: Audio packet queue is full (" << QueueSize() << "/"
                << kMaxQueuedPackets
                << " packets). Client is reading too slowly or stalled; dropping incoming audio "
                   "packets to prevent latency accumulation.";
        return;
    }

    AudioPacket packet;
    *packet.mutable_format() = format_;
    packet.set_timestamp(absl::ToUnixMicros(absl::Now()));
    packet.set_audio(reinterpret_cast<const char*>(pcm_data), num_samples * sizeof(int16_t));
    Write(std::move(packet));
}

void AudioStreamWriter::OnDone() {
    if (capture_) {
        capture_->Stop();
    }

    // The ServerWriteReactor owns the memory of this object, so we can delete it.
    delete this;
}

void AudioStreamWriter::OnCancel() {
    if (capture_) {
        capture_->Stop();
    }
    absl::MutexLock lock(&this->reactor_lock_);
    grpc::ServerWriteReactor<AudioPacket>::Finish(grpc::Status::CANCELLED);
}

}  // namespace android::emulation::control
