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
 * @file in_process_audio_source_perf.cc
 * @brief Google Benchmark suite comparing InProcessAudioSource PCM ingestion
 *        heap buffer footprint and throughput against the previous SocketBuffer
 *        + temp_frame_buffer_ implementation across 10ms, 20ms, 50ms, and 64 KiB
 *        PCM audio chunks.
 *
 * Benchmark results (--config=release, 10 repetitions, interleaved):
 * | Chunk Size       | Baseline CPU (median) | Bounded CPU (median) | Speedup | Baseline Peak Buf |
 * Bounded Peak Buf | Memory Saved |
 * |------------------|-----------------------|----------------------|---------|-------------------|------------------|--------------|
 * | 1,920 B (10ms)   | 52.7 ns               | 54.0 ns              | 1.0x    | 67,456 B          |
 * 1,920 B          | -97.2%       | | 3,840 B (20ms)   | 90.2 ns               | 63.6 ns | 1.42x |
 * 67,456 B          | 1,920 B          | -97.2%       | | 9,600 B (50ms)   | 227 ns | 89.6 ns
 * | 2.53x   | 67,456 B          | 1,920 B          | -97.2%       | | 65,536 B (64KiB) | 1,794 ns
 * | 304 ns               | 5.90x   | 132,992 B         | 1,920 B          | -98.6%       |
 */

#include <benchmark/benchmark.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/scoped_refptr.h"
#pragma clang diagnostic pop

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/socket_buffer.h"
#include "goldfish/videobridge/in_process_audio_source.h"
#include "goldfish/videobridge/managed_audio_track_source.h"

namespace goldfish::videobridge {

class InProcessAudioSourceTestPeer {
  public:
    static size_t PartialFrameCapacityBytes(const InProcessAudioSource& source)
            ABSL_LOCKS_EXCLUDED(source.buffer_mutex_) {
        const absl::MutexLock lock(&source.buffer_mutex_);
        return source.partial_frame_.capacity() * sizeof(int16_t);
    }
};

namespace {

constexpr uint32_t kSampleRateHz = 48000U;
constexpr uint32_t kChannels = 2U;
constexpr size_t kSamplesPerFrame = static_cast<size_t>(kSampleRateHz / 100U) * kChannels;
constexpr int64_t kChunk10msBytes = 1920;
constexpr int64_t kChunk20msBytes = 3840;
constexpr int64_t kChunk50msBytes = 9600;
constexpr int64_t kChunk64KiBBytes = 65536;
constexpr size_t kInitialPartialSeedSamples = kSamplesPerFrame / 2U;
constexpr int16_t kAudioSampleFillValue = 0x1A5A;

class BenchmarkAudioSink : public ::webrtc::AudioTrackSinkInterface {
  public:
    void OnData(const void* audio_data, int /*bits_per_sample*/, int /*sample_rate*/,
                size_t /*number_of_channels*/, size_t /*number_of_frames*/) override {
        const auto* samples = static_cast<const int16_t*>(audio_data);
        int16_t first_sample = samples[0];
        benchmark::DoNotOptimize(first_sample);
        frames_dispatched_++;
    }

    uint64_t FramesDispatched() const { return frames_dispatched_; }

  private:
    uint64_t frames_dispatched_ = 0;
};

// Baseline implementation reproducing the previous SocketBuffer + temp_frame_buffer_
// ring buffer logic on top of the identical ManagedAudioTrackSource dispatch path.
class BaselineSocketBufferAudioSource : public ManagedAudioTrackSource {
  public:
    BaselineSocketBufferAudioSource(uint32_t sample_rate, uint32_t channels)
            : sample_rate_(sample_rate)
            , channels_(channels)
            , samples_per_10ms_channel_(sample_rate / 100U)
            , samples_per_10ms_total_(static_cast<size_t>(sample_rate / 100U) * channels)
            , temp_frame_buffer_(samples_per_10ms_total_) {}

    void OnAudioDataBaseline(const int16_t* pcm_data, size_t num_samples)
            ABSL_LOCKS_EXCLUDED(buffer_mutex_) {
        if (!running_ || !pcm_data || num_samples == 0) {
            return;
        }

        const absl::MutexLock lock(&buffer_mutex_);
        (void)audio_buffer_.Append(pcm_data, num_samples * sizeof(int16_t));

        const size_t bytes_per_10ms = samples_per_10ms_total_ * sizeof(int16_t);
        while (audio_buffer_.Size() >= bytes_per_10ms) {
            auto peek = audio_buffer_.Peek();
            if (peek.second >= bytes_per_10ms) {
                Dispatch10msFrame(peek.first, InProcessAudioSource::kBitsPerSample, sample_rate_,
                                  channels_, samples_per_10ms_channel_);
                (void)audio_buffer_.Consume(bytes_per_10ms);
            } else {
                auto* dst = reinterpret_cast<uint8_t*>(temp_frame_buffer_.data());
                std::memcpy(dst, peek.first, peek.second);
                (void)audio_buffer_.Consume(peek.second);
                auto peek2 = audio_buffer_.Peek();
                std::memcpy(dst + peek.second, peek2.first, bytes_per_10ms - peek.second);
                Dispatch10msFrame(temp_frame_buffer_.data(), InProcessAudioSource::kBitsPerSample,
                                  sample_rate_, channels_, samples_per_10ms_channel_);
                (void)audio_buffer_.Consume(bytes_per_10ms - peek.second);
            }
        }
    }

    size_t BufferCapacityBytes() const ABSL_LOCKS_EXCLUDED(buffer_mutex_) {
        const absl::MutexLock lock(&buffer_mutex_);
        return audio_buffer_.Capacity() + (temp_frame_buffer_.capacity() * sizeof(int16_t));
    }

  protected:
    void OnStart() override { running_ = true; }
    void OnStop() override ABSL_LOCKS_EXCLUDED(buffer_mutex_) {
        running_ = false;
        const absl::MutexLock lock(&buffer_mutex_);
        audio_buffer_.Clear(/*also_free_memory=*/true);
    }

  private:
    const uint32_t sample_rate_;
    const uint32_t channels_;
    const size_t samples_per_10ms_channel_;
    const size_t samples_per_10ms_total_;

    std::atomic<bool> running_{false};
    mutable absl::Mutex buffer_mutex_;
    goldfish::SocketBuffer audio_buffer_ ABSL_GUARDED_BY(buffer_mutex_);
    std::vector<int16_t> temp_frame_buffer_ ABSL_GUARDED_BY(buffer_mutex_);
};

void BmOnAudioDataBaseline(benchmark::State& state) {
    const auto chunk_bytes = static_cast<size_t>(state.range(0));
    const size_t chunk_samples = chunk_bytes / sizeof(int16_t);
    const std::vector<int16_t> seed_samples(kInitialPartialSeedSamples, kAudioSampleFillValue);
    const std::vector<int16_t> stream_samples(chunk_samples, kAudioSampleFillValue);

    auto source =
            ::webrtc::make_ref_counted<BaselineSocketBufferAudioSource>(kSampleRateHz, kChannels);
    BenchmarkAudioSink sink;
    source->AddSink(&sink);
    source->OnAudioDataBaseline(seed_samples.data(), seed_samples.size());

    for (auto _ : state) {
        source->OnAudioDataBaseline(stream_samples.data(), stream_samples.size());
        benchmark::ClobberMemory();
    }

    state.counters["peak_buf_bytes"] = static_cast<double>(source->BufferCapacityBytes());
    benchmark::DoNotOptimize(sink.FramesDispatched());
    source->RemoveSink(&sink);
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(chunk_bytes));
}

void BmOnAudioDataBounded(benchmark::State& state) {
    const auto chunk_bytes = static_cast<size_t>(state.range(0));
    const size_t chunk_samples = chunk_bytes / sizeof(int16_t);
    const std::vector<int16_t> seed_samples(kInitialPartialSeedSamples, kAudioSampleFillValue);
    const std::vector<int16_t> stream_samples(chunk_samples, kAudioSampleFillValue);

    auto source = ::webrtc::make_ref_counted<InProcessAudioSource>(kSampleRateHz, kChannels);
    BenchmarkAudioSink sink;
    source->AddSink(&sink);
    source->OnAudioData(seed_samples.data(), seed_samples.size());

    for (auto _ : state) {
        source->OnAudioData(stream_samples.data(), stream_samples.size());
        benchmark::ClobberMemory();
    }

    state.counters["peak_buf_bytes"] =
            static_cast<double>(InProcessAudioSourceTestPeer::PartialFrameCapacityBytes(*source));
    benchmark::DoNotOptimize(sink.FramesDispatched());
    source->RemoveSink(&sink);
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(chunk_bytes));
}

BENCHMARK(BmOnAudioDataBaseline)
        ->Arg(kChunk10msBytes)
        ->Arg(kChunk20msBytes)
        ->Arg(kChunk50msBytes)
        ->Arg(kChunk64KiBBytes);

BENCHMARK(BmOnAudioDataBounded)
        ->Arg(kChunk10msBytes)
        ->Arg(kChunk20msBytes)
        ->Arg(kChunk50msBytes)
        ->Arg(kChunk64KiBBytes);

}  // namespace
}  // namespace goldfish::videobridge

BENCHMARK_MAIN();
