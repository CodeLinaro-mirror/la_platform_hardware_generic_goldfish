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

/**
 * @file grpc_audio_source_perf.cc
 * @brief Google Benchmark suite comparing GrpcAudioSource packet consumption
 *        heap buffer growth and throughput against the previous append-and-erase
 *        implementation across 20ms, 50ms, 100ms, and 64 KiB gRPC audio bursts.
 *
 * Benchmark results (`bazel run --config=release
 * @goldfish//emulator/videobridge:grpc_audio_source_perf -- --benchmark_repetitions=5`):
 * ----------------------------------------------------------------------------------------------------------------
 * Benchmark                                         Time             CPU   Iterations   Throughput
 * peak_buf_bytes
 * ----------------------------------------------------------------------------------------------------------------
 * BmConsumeAudioPacketBaseline/3840_median        252 ns          214 ns            5   16.70 Gi/s
 * 4.80 k BmConsumeAudioPacketBaseline/9600_median        268 ns          267 ns 5   33.43 Gi/s
 * 10.56 k BmConsumeAudioPacketBaseline/19200_median       541 ns          541 ns 5   33.04 Gi/s
 * 20.16 k BmConsumeAudioPacketBaseline/65536_median      3196 ns         3014 ns 5   20.25 Gi/s
 * 132.99 k BmConsumeAudioPacketBounded/3840_median         201 ns          135 ns 5   26.57 Gi/s
 * 1.92 k BmConsumeAudioPacketBounded/9600_median         325 ns          165 ns 5   54.14 Gi/s 1.92
 * k BmConsumeAudioPacketBounded/19200_median        275 ns          220 ns            5   81.38
 * Gi/s          1.92 k BmConsumeAudioPacketBounded/65536_median        890 ns          493 ns 5
 * 123.73 Gi/s          1.92 k
 */

#include <benchmark/benchmark.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/media_stream_interface.h"
#include "api/scoped_refptr.h"
#pragma clang diagnostic pop

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/synchronization/notification.h"
#include "grpc_audio_source.h"

#include "emulator_controller.pb.h"

namespace goldfish::videobridge {

class GrpcAudioSourceTestPeer {
  public:
    static void ConsumePacket(GrpcAudioSource& source, const AudioPacket& packet) {
        source.ConsumeAudioPacket(packet);
    }

    static size_t PartialFrameCapacity(const GrpcAudioSource& source) {
        return source.partial_frame_.capacity();
    }
};

namespace {

constexpr int64_t kPacket20msBytes = 3840;
constexpr int64_t kPacket50msBytes = 9600;
constexpr int64_t kPacket100msBytes = 19200;
constexpr int64_t kPacket64KiBBytes = 65536;
constexpr size_t kInitialPartialSeedBytes = GrpcAudioSource::kBytesPerFrame / 2U;
constexpr uint8_t kAudioSampleFillByte = 0x5AU;

class EmptyAudioReader : public ::grpc::ClientReaderInterface<AudioPacket> {
  public:
    explicit EmptyAudioReader(absl::Notification* finished) : finished_(finished) {}

    bool Read(AudioPacket* /*packet*/) override { return false; }
    void WaitForInitialMetadata() override {}
    bool NextMessageSize(uint32_t* /*sz*/) override { return false; }
    ::grpc::Status Finish() override {
        if (finished_ != nullptr && !finished_->HasBeenNotified()) {
            finished_->Notify();
        }
        return ::grpc::Status::OK;
    }

  private:
    absl::Notification* finished_;
};

class IdleEmulatorClient : public EmulatorClient {
  public:
    bool IsConnected() const override { return true; }
    std::string TargetAddress() const override { return "benchmark_target"; }

    std::unique_ptr<::grpc::ClientReaderInterface<AudioPacket>> StreamAudio(
            ::grpc::ClientContext* /*context*/, const AudioFormat& /*format*/) override {
        return std::make_unique<EmptyAudioReader>(&stream_finished_);
    }

    void WaitUntilIdle() { stream_finished_.WaitForNotification(); }

  private:
    absl::Notification stream_finished_;
};

class BenchmarkAudioSink : public ::webrtc::AudioTrackSinkInterface {
  public:
    void OnData(const void* audio_data, int /*bits_per_sample*/, int /*sample_rate*/,
                size_t /*number_of_channels*/, size_t /*number_of_frames*/) override {
        const auto* bytes = static_cast<const uint8_t*>(audio_data);
        uint8_t first_byte = bytes[0];
        benchmark::DoNotOptimize(first_byte);
        frames_dispatched_++;
    }

    uint64_t FramesDispatched() const { return frames_dispatched_; }

  private:
    uint64_t frames_dispatched_ = 0;
};

// Baseline implementation reproducing the previous append-and-erase partial_frame_ logic
// on top of the identical ManagedAudioTrackSource dispatch path.
class BaselineAppendEraseAudioSource : public ManagedAudioTrackSource {
  public:
    BaselineAppendEraseAudioSource() { partial_frame_.reserve(GrpcAudioSource::kBytesPerFrame); }

    void ConsumeAudioPacketBaseline(const AudioPacket& audio_packet) {
        const std::string& audio_data = audio_packet.audio();
        partial_frame_.insert(partial_frame_.end(), audio_data.begin(), audio_data.end());

        size_t bytes_consumed = 0;
        while (bytes_consumed + GrpcAudioSource::kBytesPerFrame <= partial_frame_.size()) {
            Dispatch10msFrame(partial_frame_.data() + bytes_consumed,
                              GrpcAudioSource::kBitsPerSample, GrpcAudioSource::kSampleRateHz,
                              GrpcAudioSource::kChannels, GrpcAudioSource::kSamplesPerFrame);
            bytes_consumed += GrpcAudioSource::kBytesPerFrame;
        }

        if (bytes_consumed > 0U) {
            partial_frame_.erase(partial_frame_.begin(), partial_frame_.begin() + bytes_consumed);
        }
    }

    size_t PartialFrameCapacity() const { return partial_frame_.capacity(); }

  protected:
    void OnStart() override {}
    void OnStop() override {}

  private:
    std::vector<uint8_t> partial_frame_;
};

AudioPacket MakeAudioPacket(size_t num_bytes) {
    AudioPacket packet;
    packet.set_audio(std::string(num_bytes, static_cast<char>(kAudioSampleFillByte)));
    return packet;
}

void BmConsumeAudioPacketBaseline(benchmark::State& state) {
    const auto packet_bytes = static_cast<size_t>(state.range(0));
    const AudioPacket seed_packet = MakeAudioPacket(kInitialPartialSeedBytes);
    const AudioPacket stream_packet = MakeAudioPacket(packet_bytes);

    auto source = ::webrtc::make_ref_counted<BaselineAppendEraseAudioSource>();
    BenchmarkAudioSink sink;
    source->AddSink(&sink);
    source->ConsumeAudioPacketBaseline(seed_packet);

    for (auto _ : state) {
        source->ConsumeAudioPacketBaseline(stream_packet);
        benchmark::ClobberMemory();
    }

    state.counters["peak_buf_bytes"] = static_cast<double>(source->PartialFrameCapacity());
    benchmark::DoNotOptimize(sink.FramesDispatched());
    source->RemoveSink(&sink);
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(packet_bytes));
}

void BmConsumeAudioPacketBounded(benchmark::State& state) {
    const auto packet_bytes = static_cast<size_t>(state.range(0));
    const AudioPacket seed_packet = MakeAudioPacket(kInitialPartialSeedBytes);
    const AudioPacket stream_packet = MakeAudioPacket(packet_bytes);

    auto client = std::make_shared<IdleEmulatorClient>();
    auto source = ::webrtc::make_ref_counted<GrpcAudioSource>(client);
    BenchmarkAudioSink sink;
    source->AddSink(&sink);
    client->WaitUntilIdle();
    GrpcAudioSourceTestPeer::ConsumePacket(*source, seed_packet);

    for (auto _ : state) {
        GrpcAudioSourceTestPeer::ConsumePacket(*source, stream_packet);
        benchmark::ClobberMemory();
    }

    state.counters["peak_buf_bytes"] =
            static_cast<double>(GrpcAudioSourceTestPeer::PartialFrameCapacity(*source));
    benchmark::DoNotOptimize(sink.FramesDispatched());
    source->RemoveSink(&sink);
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(packet_bytes));
}

BENCHMARK(BmConsumeAudioPacketBaseline)
        ->Arg(kPacket20msBytes)
        ->Arg(kPacket50msBytes)
        ->Arg(kPacket100msBytes)
        ->Arg(kPacket64KiBBytes);

BENCHMARK(BmConsumeAudioPacketBounded)
        ->Arg(kPacket20msBytes)
        ->Arg(kPacket50msBytes)
        ->Arg(kPacket100msBytes)
        ->Arg(kPacket64KiBBytes);

}  // namespace
}  // namespace goldfish::videobridge

BENCHMARK_MAIN();
