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

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstring>

#include "core/win/mf_video_encoder_h264.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "third_party/libyuv/include/libyuv.h"
#pragma clang diagnostic pop

namespace goldfish::videobridge {

class MFVideoEncoderH264Test {
  public:
    static HRESULT AllocateInput(MFVideoEncoderH264& encoder, int width, int height) {
        absl::MutexLock lock(&encoder.mutex_);
        return encoder.AllocateInputBuffers(width, height);
    }

    static HRESULT PrepareInput(MFVideoEncoderH264& encoder, const webrtc::VideoFrame& frame,
                                bool force_keyframe) {
        absl::MutexLock lock(&encoder.mutex_);
        return encoder.PrepareInputSample(frame, force_keyframe);
    }
};

namespace {

webrtc::VideoFrame CreateBenchmarkI420Frame(int width, int height) {
    webrtc::scoped_refptr<webrtc::I420Buffer> i420 = webrtc::I420Buffer::Create(width, height);
    std::memset(i420->MutableDataY(), 80, static_cast<size_t>(i420->StrideY()) * height);
    const int chroma_h = (height + 1) / 2;
    std::memset(i420->MutableDataU(), 110, static_cast<size_t>(i420->StrideU()) * chroma_h);
    std::memset(i420->MutableDataV(), 150, static_cast<size_t>(i420->StrideV()) * chroma_h);
    return webrtc::VideoFrame::Builder().set_video_frame_buffer(i420).set_timestamp_us(0).build();
}

void BmPrepareInputSampleReused(benchmark::State& state) {
    const int width = static_cast<int>(state.range(0));
    const int height = static_cast<int>(state.range(1));
    const uint32_t nv12_bytes = CalculateNV12BufferSize(width, height);

    MFVideoEncoderH264 encoder;
    if (FAILED(MFVideoEncoderH264Test::AllocateInput(encoder, width, height))) {
        state.SkipWithError("Failed to pre-allocate Media Foundation input sample/buffer");
        return;
    }
    webrtc::VideoFrame frame = CreateBenchmarkI420Frame(width, height);

    for (auto _ : state) {
        HRESULT hr = MFVideoEncoderH264Test::PrepareInput(encoder, frame, /*force_keyframe=*/false);
        benchmark::DoNotOptimize(hr);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * nv12_bytes);
    state.counters["peak_input_buf_bytes"] =
            benchmark::Counter(static_cast<double>(nv12_bytes), benchmark::Counter::kDefaults,
                               benchmark::Counter::OneK::kIs1024);
    state.counters["per_frame_alloc_bytes"] = benchmark::Counter(0.0);
}

void BmPrepareInputSamplePerFrameAllocBaseline(benchmark::State& state) {
    const int width = static_cast<int>(state.range(0));
    const int height = static_cast<int>(state.range(1));
    const uint32_t nv12_bytes = CalculateNV12BufferSize(width, height);

    MFVideoEncoderH264 encoder;
    if (FAILED(MFVideoEncoderH264Test::AllocateInput(encoder, width, height))) {
        state.SkipWithError("Failed to initialize Media Foundation");
        return;
    }
    webrtc::VideoFrame frame = CreateBenchmarkI420Frame(width, height);
    webrtc::scoped_refptr<webrtc::I420BufferInterface> i420 = frame.video_frame_buffer()->ToI420();

    for (auto _ : state) {
        ComPtr<IMFSample> sample;
        ComPtr<IMFMediaBuffer> media_buffer;
        MFCreateSample(&sample);
        MFCreateMemoryBuffer(nv12_bytes, &media_buffer);
        sample->AddBuffer(media_buffer.Get());

        BYTE* buffer_data = nullptr;
        DWORD max_length = 0;
        DWORD current_length = 0;
        if (SUCCEEDED(media_buffer->Lock(&buffer_data, &max_length, &current_length))) {
            libyuv::I420ToNV12(i420->DataY(), i420->StrideY(), i420->DataU(), i420->StrideU(),
                               i420->DataV(), i420->StrideV(), buffer_data, width,
                               buffer_data + (width * height), width, width, height);
            media_buffer->Unlock();
            media_buffer->SetCurrentLength(nv12_bytes);
        }
        sample->SetSampleTime(0);
        sample->SetSampleDuration(166666);
        benchmark::DoNotOptimize(sample.Get());
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * nv12_bytes);
    state.counters["peak_input_buf_bytes"] =
            benchmark::Counter(static_cast<double>(nv12_bytes) * 2.0, benchmark::Counter::kDefaults,
                               benchmark::Counter::OneK::kIs1024);
    state.counters["per_frame_alloc_bytes"] =
            benchmark::Counter(static_cast<double>(nv12_bytes), benchmark::Counter::kDefaults,
                               benchmark::Counter::OneK::kIs1024);
}

BENCHMARK(BmPrepareInputSampleReused)
        ->Args({1280, 720})
        ->Args({1920, 1080})
        ->Args({3840, 2160})
        ->Unit(benchmark::kMicrosecond);

BENCHMARK(BmPrepareInputSamplePerFrameAllocBaseline)
        ->Args({1280, 720})
        ->Args({1920, 1080})
        ->Args({3840, 2160})
        ->Unit(benchmark::kMicrosecond);

}  // namespace
}  // namespace goldfish::videobridge

BENCHMARK_MAIN();
