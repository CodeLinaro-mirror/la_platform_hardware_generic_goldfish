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
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "api/media_stream_interface.h"
#include "api/scoped_refptr.h"
#include "emulator_controller.grpc.pb.h"

namespace goldfish::videobridge {

class GrpcAudioSourceTestPeer {
  public:
    static size_t PartialFrameCapacity(const GrpcAudioSource& source) {
        return source.partial_frame_.capacity();
    }

    static size_t PartialFrameSize(const GrpcAudioSource& source) {
        return source.partial_frame_.size();
    }

    static const uint8_t* PartialFrameData(const GrpcAudioSource& source) {
        return source.partial_frame_.data();
    }
};

namespace {

class FakeAudioReader : public ::grpc::ClientReaderInterface<AudioPacket> {
  public:
    using FinishCallback = std::function<void()>;

    explicit FakeAudioReader(std::vector<AudioPacket> packets,
                             ::grpc::Status status = ::grpc::Status::OK,
                             FinishCallback on_finish = nullptr)
            : packets_(std::move(packets))
            , status_(std::move(status))
            , on_finish_(std::move(on_finish)) {}

    bool Read(AudioPacket* packet) override {
        if (index_ < packets_.size()) {
            *packet = packets_[index_++];
            return true;
        }
        return false;
    }

    void WaitForInitialMetadata() override {}

    bool NextMessageSize(uint32_t* /*sz*/) override { return false; }

    ::grpc::Status Finish() override {
        if (on_finish_) {
            on_finish_();
        }
        return status_;
    }

  private:
    std::vector<AudioPacket> packets_;
    size_t index_ = 0;
    ::grpc::Status status_;
    FinishCallback on_finish_;
};

class FakeEmulatorClient : public EmulatorClient {
  public:
    explicit FakeEmulatorClient(bool is_connected = true) : is_connected_(is_connected) {}

    bool IsConnected() const override { return is_connected_; }
    std::string TargetAddress() const override { return "fake_target_address"; }

    std::unique_ptr<::grpc::ClientReaderInterface<AudioPacket>> StreamAudio(
            ::grpc::ClientContext* /*context*/, const AudioFormat& /*format*/) override {
        audio_requested_ = true;
        std::vector<AudioPacket> packets;
        bool return_unimplemented = false;
        {
            const absl::MutexLock state_lock(&state_mutex_);
            packets = packets_;
            return_unimplemented = return_unimplemented_;
        }
        auto on_finish = [this]() {
            const absl::MutexLock state_lock(&state_mutex_);
            streams_finished_++;
        };
        if (return_unimplemented) {
            return std::make_unique<FakeAudioReader>(
                    std::vector<AudioPacket>{},
                    ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED,
                                   "Audio service unimplemented"),
                    std::move(on_finish));
        }
        return std::make_unique<FakeAudioReader>(std::move(packets), ::grpc::Status::OK,
                                                 std::move(on_finish));
    }

    void SetPackets(std::vector<AudioPacket> packets) {
        const absl::MutexLock state_lock(&state_mutex_);
        packets_ = std::move(packets);
    }

    void SetReturnUnimplemented(bool val) {
        const absl::MutexLock state_lock(&state_mutex_);
        return_unimplemented_ = val;
    }

    bool AudioRequested() const { return audio_requested_; }

    bool WaitForStreamsFinished(int expected_count, absl::Duration timeout) const {
        auto has_finished = [this, expected_count]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(state_mutex_) {
            return streams_finished_ >= expected_count;
        };
        const absl::MutexLock state_lock(&state_mutex_);
        return state_mutex_.AwaitWithTimeout(absl::Condition(&has_finished), timeout);
    }

  private:
    bool is_connected_;
    std::atomic<bool> audio_requested_{false};
    mutable absl::Mutex state_mutex_;
    bool return_unimplemented_ ABSL_GUARDED_BY(state_mutex_) = false;
    int streams_finished_ ABSL_GUARDED_BY(state_mutex_) = 0;
    std::vector<AudioPacket> packets_ ABSL_GUARDED_BY(state_mutex_);
};

class TestAudioSink : public webrtc::AudioTrackSinkInterface {
  public:
    void OnData(const void* audio_data, int bits_per_sample, int /*sample_rate*/,
                size_t number_of_channels, size_t number_of_frames) override {
        const size_t bytes_per_sample = static_cast<size_t>(bits_per_sample) / 8;
        const size_t total_bytes = number_of_frames * number_of_channels * bytes_per_sample;
        const auto* byte_ptr = static_cast<const uint8_t*>(audio_data);
        const absl::MutexLock frames_lock(&frames_mutex_);
        frame_count_++;
        total_frames_received_ += number_of_frames;
        total_samples_received_ += number_of_frames * number_of_channels;
        received_frames_.emplace_back(byte_ptr, byte_ptr + total_bytes);
    }

    int FrameCount() const {
        const absl::MutexLock frames_lock(&frames_mutex_);
        return frame_count_;
    }

    size_t TotalFramesReceived() const {
        const absl::MutexLock frames_lock(&frames_mutex_);
        return total_frames_received_;
    }

    size_t TotalSamplesReceived() const {
        const absl::MutexLock frames_lock(&frames_mutex_);
        return total_samples_received_;
    }

    std::vector<std::vector<uint8_t>> ReceivedFrames() const {
        const absl::MutexLock frames_lock(&frames_mutex_);
        return received_frames_;
    }

    bool WaitForFrames(int expected_count, absl::Duration timeout) const {
        auto has_frames = [this, expected_count]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(frames_mutex_) {
            return frame_count_ >= expected_count;
        };
        const absl::MutexLock frames_lock(&frames_mutex_);
        return frames_mutex_.AwaitWithTimeout(absl::Condition(&has_frames), timeout);
    }

  private:
    mutable absl::Mutex frames_mutex_;
    int frame_count_ ABSL_GUARDED_BY(frames_mutex_) = 0;
    size_t total_frames_received_ ABSL_GUARDED_BY(frames_mutex_) = 0;
    size_t total_samples_received_ ABSL_GUARDED_BY(frames_mutex_) = 0;
    std::vector<std::vector<uint8_t>> received_frames_ ABSL_GUARDED_BY(frames_mutex_);
};

TEST(GrpcAudioSourceTest, GrpcAudioSourceStreamsAndBuffersPackets) {
    auto client = std::make_shared<FakeEmulatorClient>(/*is_connected=*/true);

    // 3 packets of 1000 bytes each = 3000 bytes.
    // kBytesPerFrame (10ms at 48000Hz, stereo, 16-bit) = 1920 bytes.
    // 3000 bytes should yield exactly 1 frame delivered, with 1080 bytes buffered.
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
    EXPECT_EQ(sink.TotalFramesReceived(), 480);   // 480 samples per 10ms frame at 48000Hz
    EXPECT_EQ(sink.TotalSamplesReceived(), 960);  // 2 channels
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

TEST(GrpcAudioSourceTest, StopAndRestartClearsPartialFrameBuffer) {
    auto client = std::make_shared<FakeEmulatorClient>(/*is_connected=*/true);

    // First stream delivers 1000 bytes (< 1920 bytes per 10ms frame).
    AudioPacket partial_packet;
    partial_packet.set_audio(std::string(1000, '\x11'));
    client->SetPackets({partial_packet});

    auto source = ::webrtc::make_ref_counted<GrpcAudioSource>(client);
    TestAudioSink sink;
    source->AddSink(&sink);

    ASSERT_TRUE(client->WaitForStreamsFinished(1, absl::Seconds(5)));
    source->Stop();
    EXPECT_EQ(sink.FrameCount(), 0);

    // Second stream delivers one exact 10ms frame (1920 bytes) of new audio.
    AudioPacket full_frame_packet;
    full_frame_packet.set_audio(std::string(1920, '\x22'));
    client->SetPackets({full_frame_packet});

    source->Start();
    ASSERT_TRUE(sink.WaitForFrames(1, absl::Seconds(5)));
    ASSERT_TRUE(client->WaitForStreamsFinished(2, absl::Seconds(5)));

    source->Stop();
    source->RemoveSink(&sink);

    const std::vector<std::vector<uint8_t>> frames = sink.ReceivedFrames();
    ASSERT_EQ(frames.size(), 1U);
    EXPECT_EQ(frames[0], std::vector<uint8_t>(1920, 0x22));
}

TEST(GrpcAudioSourceTest, MultiFrameBurstPacketsKeepPartialBufferBoundedAtSingleFrameCapacity) {
    constexpr size_t kBurstPacketBytes = 65536U;
    constexpr size_t kBurstPacketCount = 4U;
    constexpr size_t kTotalStreamBytes = kBurstPacketBytes * kBurstPacketCount;
    constexpr size_t kExpectedFrames = kTotalStreamBytes / GrpcAudioSource::kBytesPerFrame;
    constexpr size_t kExpectedRemainderBytes = kTotalStreamBytes % GrpcAudioSource::kBytesPerFrame;
    constexpr uint8_t kFrameSeedStride = 37U;

    std::vector<uint8_t> expected_stream(kTotalStreamBytes);
    for (size_t i = 0; i < kTotalStreamBytes; ++i) {
        const size_t frame_index = i / GrpcAudioSource::kBytesPerFrame;
        expected_stream[i] = static_cast<uint8_t>((frame_index * kFrameSeedStride + i) & 0xFFU);
    }

    std::vector<AudioPacket> packets;
    packets.reserve(kBurstPacketCount);
    for (size_t p = 0; p < kBurstPacketCount; ++p) {
        AudioPacket packet;
        const size_t offset = p * kBurstPacketBytes;
        packet.set_audio(reinterpret_cast<const char*>(expected_stream.data() + offset),
                         kBurstPacketBytes);
        packets.push_back(std::move(packet));
    }

    auto client = std::make_shared<FakeEmulatorClient>(/*is_connected=*/true);
    client->SetPackets(std::move(packets));

    auto source = ::webrtc::make_ref_counted<GrpcAudioSource>(client);
    const size_t initial_capacity = GrpcAudioSourceTestPeer::PartialFrameCapacity(*source);
    const uint8_t* const initial_buffer_ptr = GrpcAudioSourceTestPeer::PartialFrameData(*source);
    ASSERT_EQ(initial_capacity, GrpcAudioSource::kBytesPerFrame);

    TestAudioSink sink;
    source->AddSink(&sink);

    ASSERT_TRUE(sink.WaitForFrames(static_cast<int>(kExpectedFrames), absl::Seconds(5)));
    ASSERT_TRUE(client->WaitForStreamsFinished(1, absl::Seconds(5)));

    source->Stop();
    source->RemoveSink(&sink);

    // Verify zero heap reallocations and bounded 10ms (1,920-byte) buffer capacity even after
    // ingesting 64 KiB multi-frame packets (97.1% smaller than a 64 KiB packet buffer).
    EXPECT_EQ(GrpcAudioSourceTestPeer::PartialFrameCapacity(*source),
              GrpcAudioSource::kBytesPerFrame);
    EXPECT_EQ(GrpcAudioSourceTestPeer::PartialFrameData(*source), initial_buffer_ptr);
    EXPECT_EQ(GrpcAudioSourceTestPeer::PartialFrameSize(*source), kExpectedRemainderBytes);

    const std::vector<std::vector<uint8_t>> frames = sink.ReceivedFrames();
    ASSERT_EQ(frames.size(), kExpectedFrames);
    for (size_t f = 0; f < kExpectedFrames; ++f) {
        const auto* frame_start = expected_stream.data() + (f * GrpcAudioSource::kBytesPerFrame);
        const std::vector<uint8_t> expected_frame(frame_start,
                                                  frame_start + GrpcAudioSource::kBytesPerFrame);
        EXPECT_EQ(frames[f], expected_frame) << "Mismatch at 10ms frame index " << f;
    }
}

TEST(GrpcAudioSourceTest, UnalignedPacketBoundariesPreserveExactByteOrderWithoutReallocating) {
    // Packet sizes exercise: empty packet, multiple sub-frame appends (500 + 600 < 1920),
    // multi-frame completion with exact zero remainder (820 + 2*1920 = 4660), and odd-byte
    // split boundary (1001 + (919 + 2*1920 = 4759)) totaling 6 exact 10ms frames (11,520 bytes).
    constexpr size_t kChunkSizes[] = {0U, 500U, 600U, 4660U, 1001U, 4759U};
    constexpr size_t kExpectedFrames = 6U;
    constexpr size_t kTotalBytes = kExpectedFrames * GrpcAudioSource::kBytesPerFrame;

    std::vector<uint8_t> expected_stream(kTotalBytes);
    for (size_t i = 0; i < kTotalBytes; ++i) {
        expected_stream[i] = static_cast<uint8_t>((i * 13U + (i >> 8U)) & 0xFFU);
    }

    std::vector<AudioPacket> packets;
    size_t offset = 0;
    for (const size_t chunk_size : kChunkSizes) {
        AudioPacket packet;
        packet.set_audio(reinterpret_cast<const char*>(expected_stream.data() + offset),
                         chunk_size);
        packets.push_back(std::move(packet));
        offset += chunk_size;
    }
    ASSERT_EQ(offset, kTotalBytes);

    auto client = std::make_shared<FakeEmulatorClient>(/*is_connected=*/true);
    client->SetPackets(std::move(packets));

    auto source = ::webrtc::make_ref_counted<GrpcAudioSource>(client);
    const uint8_t* const initial_buffer_ptr = GrpcAudioSourceTestPeer::PartialFrameData(*source);

    TestAudioSink sink;
    source->AddSink(&sink);

    ASSERT_TRUE(sink.WaitForFrames(static_cast<int>(kExpectedFrames), absl::Seconds(5)));
    ASSERT_TRUE(client->WaitForStreamsFinished(1, absl::Seconds(5)));

    source->Stop();
    source->RemoveSink(&sink);

    EXPECT_EQ(GrpcAudioSourceTestPeer::PartialFrameCapacity(*source),
              GrpcAudioSource::kBytesPerFrame);
    EXPECT_EQ(GrpcAudioSourceTestPeer::PartialFrameData(*source), initial_buffer_ptr);
    EXPECT_EQ(GrpcAudioSourceTestPeer::PartialFrameSize(*source), 0U);

    const std::vector<std::vector<uint8_t>> frames = sink.ReceivedFrames();
    ASSERT_EQ(frames.size(), kExpectedFrames);
    for (size_t f = 0; f < kExpectedFrames; ++f) {
        const auto* frame_start = expected_stream.data() + (f * GrpcAudioSource::kBytesPerFrame);
        const std::vector<uint8_t> expected_frame(frame_start,
                                                  frame_start + GrpcAudioSource::kBytesPerFrame);
        EXPECT_EQ(frames[f], expected_frame) << "Mismatch at 10ms frame index " << f;
    }
}

}  // namespace
}  // namespace goldfish::videobridge
