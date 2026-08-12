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

// Disable compiler warnings for external third-party headers.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/scoped_refptr.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "third_party/libyuv/include/libyuv.h"
#pragma clang diagnostic pop

#include <strmif.h>
#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfobjects.h>
#include <winerror.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

#define RETURN_IF_FAILED(expr) \
    do {                       \
        HRESULT _hr = (expr);  \
        if (FAILED(_hr)) {     \
            return _hr;        \
        }                      \
    } while (0)

namespace goldfish::videobridge {

MFVideoEncoderH264::MFVideoEncoderH264()
        : initialized_(false)
        , callback_(nullptr)
        , com_initialized_(false)
        , target_bitrate_bps_(kDefaultTargetBitrateBps)
        , target_fps_(kDefaultTargetFps) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) {
        com_initialized_ = true;
    } else if (hr != RPC_E_CHANGED_MODE) {
        RTC_LOG(LS_ERROR) << "Failed to initialize COM: " << hr;
        return;
    }

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        RTC_LOG(LS_ERROR) << "Failed to initialize Media Foundation: " << hr;
    }
}

MFVideoEncoderH264::~MFVideoEncoderH264() {
    Release();
    MFShutdown();
    if (com_initialized_) {
        CoUninitialize();
    }
}

int32_t MFVideoEncoderH264::InitEncode(const webrtc::VideoCodec* codec_settings,
                                       int32_t number_of_cores, size_t max_payload_size) {
    absl::MutexLock lock(&mutex_);
    if (initialized_) {
        Release();
    }

    if (!codec_settings || codec_settings->codecType != webrtc::kVideoCodecH264) {
        return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }

    codec_settings_ = *codec_settings;
    target_bitrate_bps_ = codec_settings_.startBitrate * 1000;
    target_fps_ = codec_settings_.maxFramerate;

    if (FAILED(ConfigureMFT())) {
        RTC_LOG(LS_ERROR) << "Failed to configure MFT H264 Encoder.";
        return WEBRTC_VIDEO_CODEC_ERROR;
    }

    if (FAILED(AllocateInputBuffers(codec_settings_.width, codec_settings_.height))) {
        RTC_LOG(LS_ERROR) << "Failed to allocate input buffers.";
        return WEBRTC_VIDEO_CODEC_ERROR;
    }

    if (FAILED(AllocateOutputBuffers())) {
        RTC_LOG(LS_ERROR) << "Failed to allocate output buffers.";
        return WEBRTC_VIDEO_CODEC_ERROR;
    }

    initialized_ = true;
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t MFVideoEncoderH264::RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback) {
    absl::MutexLock lock(&mutex_);
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t MFVideoEncoderH264::Release() {
    absl::MutexLock lock(&mutex_);
    if (!initialized_) {
        return WEBRTC_VIDEO_CODEC_OK;
    }

    input_sample_.Reset();
    input_buffer_.Reset();
    output_sample_.Reset();
    output_buffer_.Reset();
    input_type_.Reset();
    output_type_.Reset();
    encoder_mft_.Reset();

    initialized_ = false;
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t MFVideoEncoderH264::Encode(const webrtc::VideoFrame& frame,
                                   const std::vector<webrtc::VideoFrameType>* frame_types) {
    absl::MutexLock lock(&mutex_);
    if (!initialized_ || !callback_) {
        return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }

    // 1. Process the input frame (convert to NV12 and feed to MFT)
    HRESULT hr = ProcessInputFrame(frame);
    if (FAILED(hr)) {
        RTC_LOG(LS_ERROR) << "Failed to process input frame in MFT: " << hr;
        return WEBRTC_VIDEO_CODEC_ERROR;
    }

    // 2. Drain all encoded frames from the MFT output stream
    hr = DrainEncodedFrames(frame.rtp_timestamp(), frame.render_time_ms(), callback_);
    if (FAILED(hr)) {
        RTC_LOG(LS_ERROR) << "Failed to drain encoded frames from MFT: " << hr;
        return WEBRTC_VIDEO_CODEC_ERROR;
    }

    return WEBRTC_VIDEO_CODEC_OK;
}

void MFVideoEncoderH264::SetRates(const RateControlParameters& parameters) {
    absl::MutexLock lock(&mutex_);
    RTC_DCHECK(initialized_);

    uint32_t new_bitrate_bps = parameters.bitrate.get_sum_bps();
    uint32_t new_fps = static_cast<uint32_t>(parameters.framerate_fps);

    if (new_bitrate_bps != target_bitrate_bps_) {
        target_bitrate_bps_ = new_bitrate_bps;
        SetBitrateAndRateControl(target_bitrate_bps_);
    }
    if (new_fps != target_fps_) {
        target_fps_ = new_fps;
        // Reconfigure media types if framerate changes significantly
        ConfigureInputOutputTypes(codec_settings_.width, codec_settings_.height, target_fps_);
    }
}

void MFVideoEncoderH264::OnPacketLossRateUpdate(float packet_loss_rate) {}

void MFVideoEncoderH264::OnRttUpdate(int64_t rtt_ms) {}

void MFVideoEncoderH264::OnLossNotification(const LossNotification& loss_notification) {}

webrtc::VideoEncoder::EncoderInfo MFVideoEncoderH264::GetEncoderInfo() const {
    EncoderInfo info;
    info.supports_native_handle = false;
    info.implementation_name = "WindowsMediaFoundationH264";
    info.is_hardware_accelerated = true;
    return info;
}

HRESULT MFVideoEncoderH264::InitializeMFT() {
    MFT_REGISTER_TYPE_INFO input_info = {MFMediaType_Video, MFVideoFormat_NV12};
    MFT_REGISTER_TYPE_INFO output_info = {MFMediaType_Video, MFVideoFormat_H264};

    IMFActivate** ppActivate = nullptr;
    UINT32 count = 0;

    // Enumerate H.264 video encoders
    HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                           MFT_ENUM_FLAG_HARDWARE,  // Force hardware acceleration
                           &input_info, &output_info, &ppActivate, &count);

    if (FAILED(hr) || count == 0) {
        RTC_LOG(LS_WARNING) << "No hardware H.264 MFT found. Falling back to software MFT.";
        RETURN_IF_FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                                   MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT,
                                   &input_info, &output_info, &ppActivate, &count));
    }

    if (count > 0) {
        // Activate the first encoder MFT
        hr = ppActivate[0]->ActivateObject(IID_PPV_ARGS(&encoder_mft_));

        // Clean up activation objects
        for (UINT32 i = 0; i < count; i++) {
            ppActivate[i]->Release();
        }
        CoTaskMemFree(ppActivate);
        RETURN_IF_FAILED(hr);
    }

    // Query stream IDs
    DWORD input_count = 0;
    DWORD output_count = 0;
    RETURN_IF_FAILED(encoder_mft_->GetStreamCount(&input_count, &output_count));
    if (input_count > 0 && output_count > 0) {
        std::vector<DWORD> input_ids(input_count);
        std::vector<DWORD> output_ids(output_count);
        hr = encoder_mft_->GetStreamIDs(input_count, input_ids.data(), output_count,
                                        output_ids.data());
        if (SUCCEEDED(hr)) {
            input_stream_id_ = input_ids[0];
            output_stream_id_ = output_ids[0];
        } else if (hr == E_NOTIMPL) {
            // If GetStreamIDs is not implemented, stream IDs are 0-indexed
            input_stream_id_ = 0;
            output_stream_id_ = 0;
        } else {
            return hr;
        }
    }

    return S_OK;
}

HRESULT MFVideoEncoderH264::ConfigureMFT() {
    RETURN_IF_FAILED(InitializeMFT());
    RETURN_IF_FAILED(
            ConfigureInputOutputTypes(codec_settings_.width, codec_settings_.height, target_fps_));

    HRESULT hr = SetBitrateAndRateControl(target_bitrate_bps_);
    if (FAILED(hr)) {
        RTC_LOG(LS_WARNING) << "Failed to set bitrate/rate control, continuing with defaults: "
                            << hr;
    }
    return S_OK;
}

HRESULT MFVideoEncoderH264::ConfigureInputOutputTypes(int width, int height, int framerate) {
    // 1. Configure Output Type (H.264)
    RETURN_IF_FAILED(MFCreateMediaType(&output_type_));
    RETURN_IF_FAILED(output_type_->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    RETURN_IF_FAILED(output_type_->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
    RETURN_IF_FAILED(output_type_->SetUINT32(MF_MT_AVG_BITRATE, target_bitrate_bps_));
    RETURN_IF_FAILED(MFSetAttributeSize(output_type_.Get(), MF_MT_FRAME_SIZE, width, height));
    RETURN_IF_FAILED(MFSetAttributeRatio(output_type_.Get(), MF_MT_FRAME_RATE, framerate, 1));
    RETURN_IF_FAILED(output_type_->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));

    RETURN_IF_FAILED(encoder_mft_->SetOutputType(output_stream_id_, output_type_.Get(), 0));

    // 2. Configure Input Type (NV12)
    RETURN_IF_FAILED(MFCreateMediaType(&input_type_));
    RETURN_IF_FAILED(input_type_->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    RETURN_IF_FAILED(input_type_->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
    RETURN_IF_FAILED(MFSetAttributeSize(input_type_.Get(), MF_MT_FRAME_SIZE, width, height));
    RETURN_IF_FAILED(MFSetAttributeRatio(input_type_.Get(), MF_MT_FRAME_RATE, framerate, 1));
    RETURN_IF_FAILED(input_type_->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));

    RETURN_IF_FAILED(encoder_mft_->SetInputType(input_stream_id_, input_type_.Get(), 0));

    // Query output stream info (e.g. required buffer size)
    RETURN_IF_FAILED(encoder_mft_->GetOutputStreamInfo(output_stream_id_, &output_stream_info_));

    return S_OK;
}

HRESULT MFVideoEncoderH264::SetBitrateAndRateControl(uint32_t bitrate_bps) {
    ComPtr<ICodecAPI> codec_api;
    RETURN_IF_FAILED(encoder_mft_->QueryInterface(IID_PPV_ARGS(&codec_api)));

    // Set Rate Control Mode to RETURN_IF_FAILED (Constant Bitrate)
    VARIANT var = {};
    var.vt = VT_UI4;
    var.ulVal = eAVEncCommonRateControlMode_CBR;
    RETURN_IF_FAILED(codec_api->SetValue(&CODECAPI_AVEncCommonRateControlMode, &var));

    // Set Average Bitrate
    var.ulVal = bitrate_bps;
    RETURN_IF_FAILED(codec_api->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &var));

    // Set GOP size (Keyframe interval) to 2 seconds (2 * FPS)
    var.ulVal = target_fps_ * 2;
    RETURN_IF_FAILED(codec_api->SetValue(&CODECAPI_AVEncMPVGOPSize, &var));

    return S_OK;
}

HRESULT MFVideoEncoderH264::AllocateInputBuffers(int width, int height) {
    RETURN_IF_FAILED(MFCreateSample(&input_sample_));
    DWORD buffer_size = width * height * 3 / 2;  // NV12 size
    RETURN_IF_FAILED(MFCreateMemoryBuffer(buffer_size, &input_buffer_));
    return input_sample_->AddBuffer(input_buffer_.Get());
}

HRESULT MFVideoEncoderH264::AllocateOutputBuffers() {
    RETURN_IF_FAILED(MFCreateSample(&output_sample_));
    RETURN_IF_FAILED(MFCreateMemoryBuffer(output_stream_info_.cbSize, &output_buffer_));
    return output_sample_->AddBuffer(output_buffer_.Get());
}

HRESULT MFVideoEncoderH264::ProcessInputFrame(const webrtc::VideoFrame& frame) {
    // Convert incoming I420 frame to NV12 in our pre-allocated input buffer
    BYTE* buffer_data = nullptr;
    DWORD max_length = 0;
    DWORD current_length = 0;

    RETURN_IF_FAILED(input_buffer_->Lock(&buffer_data, &max_length, &current_length));

    webrtc::scoped_refptr<webrtc::I420BufferInterface> i420_buffer =
            frame.video_frame_buffer()->ToI420();

    int width = i420_buffer->width();
    int height = i420_buffer->height();

    uint8_t* dst_y = buffer_data;
    int dst_stride_y = width;
    uint8_t* dst_uv = buffer_data + (width * height);
    int dst_stride_uv = width;

    // Convert I420 -> NV12 using libyuv
    libyuv::I420ToNV12(i420_buffer->DataY(), i420_buffer->StrideY(), i420_buffer->DataU(),
                       i420_buffer->StrideU(), i420_buffer->DataV(), i420_buffer->StrideV(), dst_y,
                       dst_stride_y, dst_uv, dst_stride_uv, width, height);

    input_buffer_->Unlock();
    RETURN_IF_FAILED(input_buffer_->SetCurrentLength(width * height * 3 / 2));

    // Set timestamp on the sample (in 100-nanosecond units)
    RETURN_IF_FAILED(input_sample_->SetSampleTime(frame.timestamp_us() * 10));

    // Feed the sample to the MFT
    RETURN_IF_FAILED(encoder_mft_->ProcessInput(input_stream_id_, input_sample_.Get(), 0));
    return S_OK;
}

HRESULT MFVideoEncoderH264::DrainEncodedFrames(uint32_t rtp_timestamp, int64_t capture_time_ms,
                                               webrtc::EncodedImageCallback* callback) {
    HRESULT hr = S_OK;
    while (true) {
        // Reset the output buffer length before requesting new output from MFT
        if (output_buffer_) {
            output_buffer_->SetCurrentLength(0);
        }

        MFT_OUTPUT_DATA_BUFFER output_buffer = {};
        output_buffer.dwStreamID = output_stream_id_;
        output_buffer.pSample = output_sample_.Get();

        DWORD status = 0;
        hr = encoder_mft_->ProcessOutput(0, 1, &output_buffer, &status);

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            // No more frames available, break and wait for next input
            break;
        }

        if (FAILED(hr)) {
            RTC_LOG(LS_ERROR) << "MFT ProcessOutput failed: " << hr;
            return hr;
        }

        // Retrieve the encoded payload
        BYTE* data = nullptr;
        DWORD current_length = 0;
        RETURN_IF_FAILED(output_buffer_->Lock(&data, nullptr, &current_length));

        // Check if this is a keyframe (look for H.264 SPS/PPS or Keyframe attribute)
        UINT32 is_keyframe = 0;
        output_sample_->GetUINT32(MFSampleExtension_CleanPoint, &is_keyframe);

        // Wrap the payload in webrtc::EncodedImage
        webrtc::EncodedImage encoded_image;
        encoded_image.SetEncodedData(webrtc::EncodedImageBuffer::Create(data, current_length));
        encoded_image._encodedWidth = codec_settings_.width;
        encoded_image._encodedHeight = codec_settings_.height;
        encoded_image.SetRtpTimestamp(rtp_timestamp);
        encoded_image.capture_time_ms_ = capture_time_ms;
        encoded_image._frameType = is_keyframe ? webrtc::VideoFrameType::kVideoFrameKey
                                               : webrtc::VideoFrameType::kVideoFrameDelta;

        webrtc::CodecSpecificInfo codec_info;
        codec_info.codecType = webrtc::kVideoCodecH264;

        output_buffer_->Unlock();

        // Deliver the encoded image
        if (callback) {
            callback->OnEncodedImage(encoded_image, &codec_info);
        }
    }

    return S_OK;
}

}  // namespace goldfish::videobridge
