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

#include <atomic>
#include <thread>

#include <gtest/gtest.h>
#include <mferror.h>

#include "api/video_codecs/video_encoder.h"
#include "modules/video_coding/include/video_error_codes.h"

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

}  // namespace goldfish::videobridge
