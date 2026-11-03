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
#include "core/win/mf_video_encoder_h264.h"

#include <gtest/gtest.h>
#include <mferror.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/video_encoder.h"
#include "modules/video_coding/include/video_error_codes.h"
#pragma clang diagnostic pop

namespace goldfish::videobridge {
namespace {

class FakeEventGenerator final : public IMFMediaEventGenerator {
  public:
    void Enqueue(MediaEventType type) {
        absl::MutexLock lock(&mu_);
        events_.push_back(type);
    }

    int GetEventCallCount() const {
        absl::MutexLock lock(&mu_);
        return get_event_calls_;
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IMFMediaEventGenerator) {
            *ppv = static_cast<IMFMediaEventGenerator*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count_; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG count = --ref_count_;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    STDMETHODIMP GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent) override {
        absl::MutexLock lock(&mu_);
        ++get_event_calls_;
        if ((dwFlags & MF_EVENT_FLAG_NO_WAIT) == 0) {
            return E_INVALIDARG;
        }
        if (events_.empty()) {
            return MF_E_NO_EVENTS_AVAILABLE;
        }
        MediaEventType type = events_.front();
        events_.erase(events_.begin());
        PROPVARIANT val;
        PropVariantInit(&val);
        return MFCreateMediaEvent(type, GUID_NULL, S_OK, &val, ppEvent);
    }
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback*, IUnknown*) override { return E_NOTIMPL; }
    STDMETHODIMP EndGetEvent(IMFAsyncResult*, IMFMediaEvent**) override { return E_NOTIMPL; }
    STDMETHODIMP QueueEvent(MediaEventType, REFGUID, HRESULT, const PROPVARIANT*) override {
        return E_NOTIMPL;
    }

  private:
    std::atomic<ULONG> ref_count_{1};
    mutable absl::Mutex mu_;
    std::vector<MediaEventType> events_ ABSL_GUARDED_BY(mu_);
    int get_event_calls_ ABSL_GUARDED_BY(mu_) = 0;
};

}  // namespace

class MFVideoEncoderH264Test : public ::testing::Test {
  protected:
    void SetUp() override { encoder_ = std::make_unique<MFVideoEncoderH264>(); }

    void CacheHeader(const std::vector<uint8_t>& header) {
        absl::MutexLock lock(&encoder_->mutex_);
        encoder_->CachePendingHeaderData(header.data(), static_cast<DWORD>(header.size()));
    }

    size_t PendingHeaderSize() {
        absl::MutexLock lock(&encoder_->mutex_);
        return encoder_->pending_header_data_.size();
    }

    webrtc::scoped_refptr<webrtc::EncodedImageBuffer> BuildBuffer(const std::vector<uint8_t>& vcl,
                                                                  bool is_keyframe, bool has_sps) {
        absl::MutexLock lock(&encoder_->mutex_);
        return encoder_->BuildEncodedBuffer(vcl.data(), static_cast<DWORD>(vcl.size()), is_keyframe,
                                            has_sps);
    }

    void AttachFakeAsyncEventGenerator(FakeEventGenerator* generator) {
        absl::MutexLock lock(&encoder_->mutex_);
        encoder_->initialized_ = true;
        encoder_->is_async_mft_ = true;
        generator->AddRef();
        *encoder_->event_generator_.GetAddressOf() = generator;
    }

    HRESULT PollAsyncEvent(int max_wait_attempts, MediaEventType* out_type) {
        absl::MutexLock lock(&encoder_->mutex_);
        return encoder_->PollNextAsyncEvent(max_wait_attempts, out_type);
    }

    bool WaitForOutputEvent(bool drained_any) {
        absl::MutexLock lock(&encoder_->mutex_);
        return encoder_->WaitForAsyncOutputEvent(drained_any);
    }

    uint32_t PendingOutputCount() {
        absl::MutexLock lock(&encoder_->mutex_);
        return encoder_->pending_output_count_;
    }

    HRESULT AllocateInput(int width, int height) {
        absl::MutexLock lock(&encoder_->mutex_);
        return encoder_->AllocateInputBuffers(width, height);
    }

    HRESULT PrepareInput(const webrtc::VideoFrame& frame, bool force_keyframe) {
        absl::MutexLock lock(&encoder_->mutex_);
        return encoder_->PrepareInputSample(frame, force_keyframe);
    }

    IMFSample* InputSample() {
        absl::MutexLock lock(&encoder_->mutex_);
        return encoder_->input_sample_.Get();
    }

    IMFMediaBuffer* InputBuffer() {
        absl::MutexLock lock(&encoder_->mutex_);
        return encoder_->input_buffer_.Get();
    }

    void SetInputStreamMinSize(DWORD min_size) {
        absl::MutexLock lock(&encoder_->mutex_);
        encoder_->input_stream_info_.cbSize = min_size;
    }

    static webrtc::VideoFrame CreateSolidI420Frame(int width, int height, uint8_t y_val,
                                                   uint8_t u_val, uint8_t v_val,
                                                   int64_t timestamp_us) {
        webrtc::scoped_refptr<webrtc::I420Buffer> i420 = webrtc::I420Buffer::Create(width, height);
        for (int r = 0; r < height; ++r) {
            std::memset(i420->MutableDataY() + r * i420->StrideY(), y_val, width);
        }
        const int chroma_w = (width + 1) / 2;
        const int chroma_h = (height + 1) / 2;
        for (int r = 0; r < chroma_h; ++r) {
            std::memset(i420->MutableDataU() + r * i420->StrideU(), u_val, chroma_w);
            std::memset(i420->MutableDataV() + r * i420->StrideV(), v_val, chroma_w);
        }
        return webrtc::VideoFrame::Builder()
                .set_video_frame_buffer(i420)
                .set_timestamp_us(timestamp_us)
                .build();
    }

    std::unique_ptr<MFVideoEncoderH264> encoder_;
};

TEST_F(MFVideoEncoderH264Test, InitEncodeFailsWithInvalidCodec) {
    webrtc::VideoCodec settings;
    settings.codecType = webrtc::kVideoCodecVP8;  // Only H264 is supported
    settings.width = 640;
    settings.height = 480;
    settings.startBitrate = 3000;
    settings.maxFramerate = 30;

    int32_t result = encoder_->InitEncode(&settings, 1, 1200);
    EXPECT_EQ(result, WEBRTC_VIDEO_CODEC_ERR_PARAMETER);
}

TEST_F(MFVideoEncoderH264Test, InitEncodeFailsWithNullSettings) {
    int32_t result = encoder_->InitEncode(nullptr, 1, 1200);
    EXPECT_EQ(result, WEBRTC_VIDEO_CODEC_ERR_PARAMETER);
}

TEST_F(MFVideoEncoderH264Test, GetEncoderInfoReturnsCorrectMetadata) {
    webrtc::VideoEncoder::EncoderInfo info = encoder_->GetEncoderInfo();
    EXPECT_FALSE(info.supports_native_handle);
    EXPECT_EQ(info.implementation_name, "WindowsMediaFoundationH264");
    EXPECT_TRUE(info.is_hardware_accelerated);
}

TEST_F(MFVideoEncoderH264Test, InspectH264BitstreamClassifiesNaluTypes) {
    // Null or short buffer
    H264BitstreamInfo empty_info = InspectH264Bitstream(nullptr, 0);
    EXPECT_FALSE(empty_info.has_sps);
    EXPECT_FALSE(empty_info.has_vcl_nal);
    EXPECT_FALSE(empty_info.is_keyframe);

    // Standalone SPS (NAL type 7) + PPS (NAL type 8) with 4-byte and 3-byte start codes
    const std::vector<uint8_t> sps_pps = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00,
                                          0x1E, 0x00, 0x00, 0x01, 0x68, 0xCE, 0x3C};
    H264BitstreamInfo header_info =
            InspectH264Bitstream(sps_pps.data(), static_cast<DWORD>(sps_pps.size()));
    EXPECT_TRUE(header_info.has_sps);
    EXPECT_FALSE(header_info.has_vcl_nal);
    EXPECT_TRUE(header_info.is_keyframe);

    // Standalone IDR slice (NAL type 5)
    const std::vector<uint8_t> idr_slice = {0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84};
    H264BitstreamInfo idr_info =
            InspectH264Bitstream(idr_slice.data(), static_cast<DWORD>(idr_slice.size()));
    EXPECT_FALSE(idr_info.has_sps);
    EXPECT_TRUE(idr_info.has_vcl_nal);
    EXPECT_TRUE(idr_info.is_keyframe);

    // Non-IDR P-slice (NAL type 1)
    const std::vector<uint8_t> p_slice = {0x00, 0x00, 0x01, 0x41, 0x9A, 0x24};
    H264BitstreamInfo p_info =
            InspectH264Bitstream(p_slice.data(), static_cast<DWORD>(p_slice.size()));
    EXPECT_FALSE(p_info.has_sps);
    EXPECT_TRUE(p_info.has_vcl_nal);
    EXPECT_FALSE(p_info.is_keyframe);
}

TEST_F(MFVideoEncoderH264Test, CoalescesStandaloneHeaderWithNextVclSampleAndCapsSize) {
    const std::vector<uint8_t> sps_pps = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42,
                                          0x00, 0x00, 0x00, 0x01, 0x68, 0xCE};
    const std::vector<uint8_t> idr_slice = {0x00, 0x00, 0x00, 0x01, 0x65, 0x88};

    CacheHeader(sps_pps);
    EXPECT_EQ(PendingHeaderSize(), sps_pps.size());

    auto coalesced = BuildBuffer(idr_slice, /*is_keyframe=*/true, /*has_sps=*/false);
    ASSERT_NE(coalesced, nullptr);
    EXPECT_EQ(coalesced->size(), sps_pps.size() + idr_slice.size());
    EXPECT_EQ(PendingHeaderSize(), 0U);

    std::vector<uint8_t> expected = sps_pps;
    expected.insert(expected.end(), idr_slice.begin(), idr_slice.end());
    EXPECT_EQ(std::vector<uint8_t>(coalesced->data(), coalesced->data() + coalesced->size()),
              expected);

    // Verify cap prevents unbounded growth
    const std::vector<uint8_t> chunk(MFVideoEncoderH264::kMaxPendingHeaderBytes / 2 + 100, 0xAB);
    CacheHeader(chunk);
    EXPECT_EQ(PendingHeaderSize(), chunk.size());
    CacheHeader(chunk);
    EXPECT_EQ(PendingHeaderSize(), chunk.size());
}

TEST_F(MFVideoEncoderH264Test, CalculateNV12BufferSizeHandlesOddHeight) {
    EXPECT_EQ(CalculateNV12BufferSize(0, 480), 0U);
    EXPECT_EQ(CalculateNV12BufferSize(640, 0), 0U);
    EXPECT_EQ(CalculateNV12BufferSize(-640, 480), 0U);
    EXPECT_EQ(CalculateNV12BufferSize(640, -480), 0U);
    EXPECT_EQ(CalculateNV12BufferSize(640, 480), 640U * 480U + 640U * 240U);
    EXPECT_EQ(CalculateNV12BufferSize(640, 481), 640U * 481U + 640U * 241U);
}

TEST_F(MFVideoEncoderH264Test, WaitForAsyncOutputEventConsumesDequeuedOutputWithoutRepolling) {
    ComPtr<FakeEventGenerator> generator(new FakeEventGenerator());
    generator->Release();  // ComPtr holds the sole reference
    AttachFakeAsyncEventGenerator(generator.Get());

    generator->Enqueue(METransformHaveOutput);
    MediaEventType event_type = MEUnknown;
    EXPECT_EQ(PollAsyncEvent(/*max_wait_attempts=*/0, &event_type), S_OK);
    EXPECT_EQ(event_type, METransformHaveOutput);
    EXPECT_EQ(PendingOutputCount(), 1U);
    EXPECT_EQ(generator->GetEventCallCount(), 1);

    // WaitForAsyncOutputEvent should immediately consume the pending output event
    // without calling GetEvent again or polling for 50ms.
    EXPECT_TRUE(WaitForOutputEvent(/*drained_any=*/false));
    EXPECT_EQ(PendingOutputCount(), 0U);
    EXPECT_EQ(generator->GetEventCallCount(), 1);
}

TEST_F(MFVideoEncoderH264Test, PollNextAsyncEventYieldsMutexAndWakesOnConcurrentRelease) {
    ComPtr<FakeEventGenerator> generator(new FakeEventGenerator());
    generator->Release();
    AttachFakeAsyncEventGenerator(generator.Get());

    std::thread releaser([this, &generator]() {
        while (generator->GetEventCallCount() == 0) {
            std::this_thread::yield();
        }
        EXPECT_EQ(encoder_->Release(), WEBRTC_VIDEO_CODEC_OK);
    });

    MediaEventType event_type = MEUnknown;
    HRESULT hr = PollAsyncEvent(/*max_wait_attempts=*/500, &event_type);
    releaser.join();
    EXPECT_EQ(hr, MF_E_SHUTDOWN);
}

TEST_F(MFVideoEncoderH264Test, PrepareInputSampleReusesPreallocatedSampleAndBufferAcrossFrames) {
    constexpr int kWidth = 1280;
    constexpr int kHeight = 720;
    constexpr int kFrameCount = 60;
    const uint32_t expected_nv12_bytes = CalculateNV12BufferSize(kWidth, kHeight);
    ASSERT_EQ(expected_nv12_bytes, 1382400U);

    ASSERT_EQ(AllocateInput(kWidth, kHeight), S_OK);
    IMFSample* const initial_sample = InputSample();
    IMFMediaBuffer* const initial_buffer = InputBuffer();
    ASSERT_NE(initial_sample, nullptr);
    ASSERT_NE(initial_buffer, nullptr);

    LONGLONG prev_sample_time = -1;
    for (int i = 0; i < kFrameCount; ++i) {
        const uint8_t y_val = static_cast<uint8_t>(16 + (i % 200));
        const uint8_t u_val = static_cast<uint8_t>(64 + (i % 128));
        const uint8_t v_val = static_cast<uint8_t>(128 + (i % 64));
        const bool force_keyframe = (i % 15 == 0);
        webrtc::VideoFrame frame =
                CreateSolidI420Frame(kWidth, kHeight, y_val, u_val, v_val, i * 16667LL);

        ASSERT_EQ(PrepareInput(frame, force_keyframe), S_OK);
        EXPECT_EQ(InputSample(), initial_sample);
        EXPECT_EQ(InputBuffer(), initial_buffer);

        DWORD current_length = 0;
        DWORD max_length = 0;
        BYTE* data = nullptr;
        ASSERT_EQ(initial_buffer->Lock(&data, &max_length, &current_length), S_OK);
        EXPECT_GE(max_length, expected_nv12_bytes);
        EXPECT_EQ(current_length, expected_nv12_bytes);
        EXPECT_EQ(data[0], y_val);
        EXPECT_EQ(data[kWidth * kHeight - 1], y_val);
        EXPECT_EQ(data[kWidth * kHeight], u_val);
        EXPECT_EQ(data[kWidth * kHeight + 1], v_val);
        ASSERT_EQ(initial_buffer->Unlock(), S_OK);

        UINT32 clean_point = 0;
        const HRESULT clean_hr =
                initial_sample->GetUINT32(MFSampleExtension_CleanPoint, &clean_point);
        if (force_keyframe) {
            EXPECT_EQ(clean_hr, S_OK);
            EXPECT_EQ(clean_point, 1U);
        } else {
            EXPECT_EQ(clean_hr, MF_E_ATTRIBUTENOTFOUND);
        }

        LONGLONG sample_time = 0;
        LONGLONG sample_duration = 0;
        EXPECT_EQ(initial_sample->GetSampleTime(&sample_time), S_OK);
        EXPECT_EQ(initial_sample->GetSampleDuration(&sample_duration), S_OK);
        EXPECT_GT(sample_time, prev_sample_time);
        EXPECT_GT(sample_duration, 0);
        prev_sample_time = sample_time;
    }
}

TEST_F(MFVideoEncoderH264Test, PrepareInputSampleReallocatesWhenResolutionGrowsOrBufferInUse) {
    EXPECT_EQ(AllocateInput(0, 0), E_INVALIDARG);

    webrtc::VideoFrame small_frame = CreateSolidI420Frame(640, 360, 40, 100, 150, 0);
    ASSERT_EQ(PrepareInput(small_frame, /*force_keyframe=*/true), S_OK);
    IMFMediaBuffer* const small_buffer = InputBuffer();
    ASSERT_NE(small_buffer, nullptr);
    DWORD max_length = 0;
    ASSERT_EQ(small_buffer->GetMaxLength(&max_length), S_OK);
    EXPECT_EQ(max_length, CalculateNV12BufferSize(640, 360));

    // Larger frame triggers reallocation to fit 1280x720 NV12 payload.
    webrtc::VideoFrame large_frame = CreateSolidI420Frame(1280, 720, 80, 110, 160, 16667);
    ASSERT_EQ(PrepareInput(large_frame, /*force_keyframe=*/false), S_OK);
    IMFMediaBuffer* const large_buffer = InputBuffer();
    ASSERT_NE(large_buffer, nullptr);
    ASSERT_EQ(large_buffer->GetMaxLength(&max_length), S_OK);
    EXPECT_EQ(max_length, CalculateNV12BufferSize(1280, 720));

    // Subsequent smaller frame reuses the existing larger capacity without reallocating.
    ASSERT_EQ(PrepareInput(small_frame, /*force_keyframe=*/false), S_OK);
    EXPECT_EQ(InputBuffer(), large_buffer);
    DWORD current_length = 0;
    ASSERT_EQ(InputBuffer()->GetCurrentLength(&current_length), S_OK);
    EXPECT_EQ(current_length, CalculateNV12BufferSize(640, 360));

    // When an async MFT still holds a COM reference to the input sample, PrepareInputSample
    // allocates a fresh sample/buffer so the in-flight frame is not overwritten.
    ComPtr<IMFSample> in_flight_sample(InputSample());
    ComPtr<IMFMediaBuffer> in_flight_buffer(InputBuffer());
    webrtc::VideoFrame next_frame = CreateSolidI420Frame(640, 360, 200, 90, 210, 33334);
    ASSERT_EQ(PrepareInput(next_frame, /*force_keyframe=*/false), S_OK);
    EXPECT_NE(InputSample(), in_flight_sample.Get());
    EXPECT_NE(InputBuffer(), in_flight_buffer.Get());

    BYTE* in_flight_data = nullptr;
    ASSERT_EQ(in_flight_buffer->Lock(&in_flight_data, nullptr, &current_length), S_OK);
    EXPECT_EQ(in_flight_data[0], 40);
    ASSERT_EQ(in_flight_buffer->Unlock(), S_OK);

    // Honors MFT minimum input buffer size (cbSize) when larger than tight NV12 size.
    SetInputStreamMinSize(2097152U);
    ASSERT_EQ(PrepareInput(next_frame, /*force_keyframe=*/false), S_OK);
    ASSERT_EQ(InputBuffer()->GetMaxLength(&max_length), S_OK);
    EXPECT_EQ(max_length, 2097152U);
}

}  // namespace goldfish::videobridge
