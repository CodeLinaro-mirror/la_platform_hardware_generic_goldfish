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
#include "grpc_audio_source.h"

#include <algorithm>
#include <utility>

#include "absl/log/log.h"

namespace goldfish::videobridge {

constexpr int32_t kSampleRateHz = 44100;
constexpr int32_t kChannels = 2;
constexpr int32_t kBytesPerSample = 2;

// WebRTC requires each audio packet to be exactly 10ms long.
constexpr int32_t kSamplesPerFrame = kSampleRateHz / 100;  // (10ms / 1s)
constexpr size_t kBytesPerFrame =
        static_cast<size_t>(kBytesPerSample) * kSamplesPerFrame * kChannels;

GrpcAudioSource::GrpcAudioSource(std::shared_ptr<EmulatorClient> client)
        : client_(std::move(client)) {
    partial_frame_.reserve(kBytesPerFrame);
}

GrpcAudioSource::~GrpcAudioSource() {
    Stop();
}

void GrpcAudioSource::AddSink(::webrtc::AudioTrackSinkInterface* sink) {
    const absl::MutexLock lock(&sinks_mutex_);
    sinks_.insert(sink);
}

void GrpcAudioSource::RemoveSink(::webrtc::AudioTrackSinkInterface* sink) {
    const absl::MutexLock lock(&sinks_mutex_);
    sinks_.erase(sink);
}

const ::webrtc::AudioOptions GrpcAudioSource::options() const {
    ::webrtc::AudioOptions options;
    options.echo_cancellation = false;
    options.auto_gain_control = false;
    options.noise_suppression = false;
    options.highpass_filter = false;
    return options;
}

void GrpcAudioSource::Start() {
    if (!client_ || !client_->IsConnected()) {
        LOG(ERROR) << "Failed to start audio capture: Emulator client is disconnected. "
                   << "Ensure the emulator is running and reachable.";
        running_ = false;
        return;
    }

    bool expected = false;
    if (running_.compare_exchange_strong(expected, true)) {
        LOG(INFO) << "Starting GrpcAudioSource capture loop connected to emulator at "
                  << client_->TargetAddress();
        capture_thread_ = std::thread([this]() { CaptureLoop(); });
    }
}

void GrpcAudioSource::Stop() {
    bool expected = true;
    if (running_.compare_exchange_strong(expected, false)) {
        VLOG(1) << "Stopping GrpcAudioSource capture loop connected to emulator at "
                << client_->TargetAddress();
        context_.TryCancel();
    }
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
}

void GrpcAudioSource::CaptureLoop() {
    AudioFormat request_format;
    request_format.set_format(AudioFormat::AUD_FMT_S16);
    request_format.set_channels(AudioFormat::Stereo);
    request_format.set_samplingrate(kSampleRateHz);

    auto reader = client_->StreamAudio(&context_, request_format);
    if (!reader) {
        LOG(ERROR) << "Failed to open gRPC audio stream. Verify network or emulator state.";
        running_ = false;
        return;
    }

    AudioPacket packet;
    while (running_ && reader->Read(&packet)) {
        ConsumeAudioPacket(packet);
    }

    const ::grpc::Status status = reader->Finish();
    if (!status.ok()) {
        if (status.error_code() == ::grpc::StatusCode::UNIMPLEMENTED) {
            LOG(WARNING) << "Emulator audio streaming service is not supported/implemented. "
                         << "Audio streaming will be disabled.";
        } else if (status.error_code() != ::grpc::StatusCode::CANCELLED) {
            LOG(ERROR) << "Audio stream closed with gRPC error: " << status.error_message()
                       << " (code: " << status.error_code() << ")";
        }
    }

    LOG(INFO) << "GrpcAudioSource capture loop exited connected to emulator at "
              << client_->TargetAddress();
    running_ = false;
}

void GrpcAudioSource::ConsumeAudioPacket(const AudioPacket& audio_packet) {
    const std::string& audio_data = audio_packet.audio();
    partial_frame_.insert(partial_frame_.end(), audio_data.begin(), audio_data.end());

    size_t bytes_consumed = 0;
    while (bytes_consumed + kBytesPerFrame <= partial_frame_.size()) {
        DeliverFrame(partial_frame_.data() + bytes_consumed, kBytesPerSample * 8, kSampleRateHz,
                     kChannels, kSamplesPerFrame);
        bytes_consumed += kBytesPerFrame;
    }

    if (bytes_consumed > 0) {
        partial_frame_.erase(partial_frame_.begin(), partial_frame_.begin() + bytes_consumed);
    }
}

void GrpcAudioSource::DeliverFrame(const void* audio_data, int bits_per_sample, int sample_rate,
                                   size_t number_of_channels, size_t number_of_frames) {
    const absl::MutexLock lock(sinks_mutex_);
    for (auto* sink : sinks_) {
        sink->OnData(audio_data, bits_per_sample, sample_rate, number_of_channels,
                     number_of_frames);
    }
}

}  // namespace goldfish::videobridge
