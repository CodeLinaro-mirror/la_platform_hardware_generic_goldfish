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

#include <cstdint>
#include <utility>

#include "absl/log/log.h"

namespace goldfish::videobridge {

GrpcAudioSource::GrpcAudioSource(std::shared_ptr<EmulatorClient> client)
        : client_(std::move(client)) {
    partial_frame_.reserve(kBytesPerFrame);
}

GrpcAudioSource::~GrpcAudioSource() {
    OnStop();
}

void GrpcAudioSource::OnStart() {
    if (!client_ || !client_->IsConnected()) {
        LOG(ERROR) << "Failed to start audio capture: Emulator client is disconnected. "
                   << "Ensure the emulator is running and reachable.";
        return;
    }

    bool expected = false;
    if (capture_running_.compare_exchange_strong(expected, true)) {
        LOG(INFO) << "Starting GrpcAudioSource capture loop connected to emulator at "
                  << client_->TargetAddress();
        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }
        context_ = std::make_unique<::grpc::ClientContext>();
        capture_thread_ = std::thread([this]() { CaptureLoop(); });
    }
}

void GrpcAudioSource::OnStop() {
    bool expected = true;
    if (capture_running_.compare_exchange_strong(expected, false)) {
        VLOG(1) << "Stopping GrpcAudioSource capture loop connected to emulator at "
                << client_->TargetAddress();
        if (context_) {
            context_->TryCancel();
        }
    }
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
}

void GrpcAudioSource::CaptureLoop() {
    partial_frame_.clear();

    AudioFormat request_format;
    request_format.set_format(AudioFormat::AUD_FMT_S16);
    request_format.set_channels(AudioFormat::Stereo);
    request_format.set_samplingrate(kSampleRateHz);

    auto reader = client_->StreamAudio(context_.get(), request_format);
    if (!reader) {
        LOG(ERROR) << "Failed to open gRPC audio stream. Verify network or emulator state.";
        capture_running_ = false;
        return;
    }

    AudioPacket packet;
    while (capture_running_ && reader->Read(&packet)) {
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
    capture_running_ = false;
}

void GrpcAudioSource::ConsumeAudioPacket(const AudioPacket& audio_packet) {
    const std::string& audio_data = audio_packet.audio();
    if (audio_data.empty()) {
        return;
    }

    const auto* src = reinterpret_cast<const uint8_t*>(audio_data.data());
    size_t remaining = audio_data.size();

    if (!partial_frame_.empty()) {
        const size_t bytes_needed = kBytesPerFrame - partial_frame_.size();
        if (remaining < bytes_needed) {
            partial_frame_.insert(partial_frame_.end(), src, src + remaining);
            return;
        }
        partial_frame_.insert(partial_frame_.end(), src, src + bytes_needed);
        DispatchFrame(partial_frame_.data());
        partial_frame_.clear();
        src += bytes_needed;
        remaining -= bytes_needed;
    }

    const bool is_sample_aligned = (reinterpret_cast<uintptr_t>(src) % alignof(int16_t)) == 0U;
    while (remaining >= kBytesPerFrame) {
        if (is_sample_aligned) {
            DispatchFrame(src);
        } else {
            partial_frame_.assign(src, src + kBytesPerFrame);
            DispatchFrame(partial_frame_.data());
            partial_frame_.clear();
        }
        src += kBytesPerFrame;
        remaining -= kBytesPerFrame;
    }

    if (remaining > 0U) {
        partial_frame_.assign(src, src + remaining);
    }
}

void GrpcAudioSource::DispatchFrame(const uint8_t* frame_data) {
    Dispatch10msFrame(frame_data, kBitsPerSample, kSampleRateHz, kChannels, kSamplesPerFrame);
}

}  // namespace goldfish::videobridge
