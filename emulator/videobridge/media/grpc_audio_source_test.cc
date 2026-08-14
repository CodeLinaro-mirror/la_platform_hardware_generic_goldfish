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

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/synchronization/mutex.h"

#include "api/media_stream_interface.h"
#include "api/scoped_refptr.h"
#include "emulator_controller.grpc.pb.h"

namespace goldfish::videobridge {
namespace {

class FakeAudioReader : public ::grpc::ClientReaderInterface<AudioPacket> {
  public:
    explicit FakeAudioReader(std::vector<AudioPacket> packets,
                             ::grpc::Status status = ::grpc::Status::OK)
            : packets_(std::move(packets)), status_(status) {}

    bool Read(AudioPacket* packet) override {
        if (index_ < packets_.size()) {
            *packet = packets_[index_++];
            return true;
        }
        return false;
    }

    void WaitForInitialMetadata() override {}

    bool NextMessageSize(uint32_t* /*sz*/) override { return false; }

    ::grpc::Status Finish() override { return status_; }

  private:
    std::vector<AudioPacket> packets_;
    size_t index_ = 0;
    ::grpc::Status status_;
};

class FakeEmulatorClient : public EmulatorClient {
  public:
    explicit FakeEmulatorClient(bool is_connected = true) : is_connected_(is_connected) {}

    bool IsConnected() const override { return is_connected_; }
    std::string TargetAddress() const override { return "fake_target_address"; }

    std::unique_ptr<::grpc::ClientReaderInterface<AudioPacket>> StreamAudio(
            ::grpc::ClientContext* /*context*/, const AudioFormat& /*format*/) override {
        audio_requested_ = true;
        if (return_unimplemented_) {
            return std::make_unique<FakeAudioReader>(
                    std::vector<AudioPacket>{}, ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED,
                                                               "Audio service unimplemented"));
        }
        return std::make_unique<FakeAudioReader>(packets_);
    }

    void SetPackets(std::vector<AudioPacket> packets) { packets_ = std::move(packets); }
    void SetReturnUnimplemented(bool val) { return_unimplemented_ = val; }
    bool AudioRequested() const { return audio_requested_; }

  private:
    bool is_connected_;
    bool return_unimplemented_ = false;
    std::atomic<bool> audio_requested_{false};
    std::vector<AudioPacket> packets_;
};

class TestAudioSink : public webrtc::AudioTrackSinkInterface {
  public:
    void OnData(const void* /*audio_data*/, int /*bits_per_sample*/, int /*sample_rate*/,
                size_t number_of_channels, size_t number_of_frames) override {
        const absl::MutexLock lock(&mutex_);
        frame_count_++;
        total_frames_received_ += number_of_frames;
        total_samples_received_ += number_of_frames * number_of_channels;
    }

    int FrameCount() const {
        const absl::MutexLock lock(&mutex_);
        return frame_count_;
    }

    size_t TotalFramesReceived() const {
        const absl::MutexLock lock(&mutex_);
        return total_frames_received_;
    }

    size_t TotalSamplesReceived() const {
        const absl::MutexLock lock(&mutex_);
        return total_samples_received_;
    }

  private:
    mutable absl::Mutex mutex_;
    int frame_count_ ABSL_GUARDED_BY(mutex_) = 0;
    size_t total_frames_received_ ABSL_GUARDED_BY(mutex_) = 0;
    size_t total_samples_received_ ABSL_GUARDED_BY(mutex_) = 0;
};

TEST(GrpcAudioSourceTest, GrpcAudioSourceStreamsAndBuffersPackets) {
    auto client = std::make_shared<FakeEmulatorClient>(/*is_connected=*/true);

    // 3 packets of 1000 bytes each = 3000 bytes.
    // kBytesPerFrame (10ms at 44100Hz, stereo, 16-bit) = 1764 bytes.
    // 3000 bytes should yield exactly 1 frame delivered, with 1236 bytes buffered.
    std::vector<AudioPacket> packets(3);
    for (auto& packet : packets) {
        packet.set_audio(std::string(1000, '\x0A'));
    }
    client->SetPackets(std::move(packets));

    auto source = ::webrtc::make_ref_counted<GrpcAudioSource>(client);
    TestAudioSink sink;
    source->AddSink(&sink);

    source->Start();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (sink.FrameCount() < 1 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    source->Stop();
    source->RemoveSink(&sink);

    EXPECT_EQ(sink.FrameCount(), 1);
    EXPECT_EQ(sink.TotalFramesReceived(), 441);   // 441 samples per 10ms frame at 44100Hz
    EXPECT_EQ(sink.TotalSamplesReceived(), 882);  // 2 channels
    EXPECT_TRUE(client->AudioRequested());
}

TEST(GrpcAudioSourceTest, GracefullyHandlesUnimplementedAudioStream) {
    auto client = std::make_shared<FakeEmulatorClient>(/*is_connected=*/true);
    client->SetReturnUnimplemented(true);

    auto source = ::webrtc::make_ref_counted<GrpcAudioSource>(client);
    TestAudioSink sink;
    source->AddSink(&sink);

    source->Start();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!client->AudioRequested() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    source->Stop();
    source->RemoveSink(&sink);

    EXPECT_EQ(sink.FrameCount(), 0);
    EXPECT_TRUE(client->AudioRequested());
}

}  // namespace
}  // namespace goldfish::videobridge
