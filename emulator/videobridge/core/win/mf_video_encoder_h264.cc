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
#include <algorithm>
#include <iostream>

#include "api/scoped_refptr.h"
#include "api/video_codecs/h264_profile_level_id.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "third_party/libyuv/include/libyuv.h"
#pragma clang diagnostic pop

#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <strmif.h>
#include <winerror.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

#define RETURN_IF_FAILED(expr)                                                                  \
    do {                                                                                        \
        HRESULT _hr = (expr);                                                                   \
        if (FAILED(_hr)) {                                                                      \
            RTC_LOG(LS_ERROR) << #expr << " failed: 0x" << std::hex << _hr << std::dec;         \
            std::cerr << "[MF ERROR] " << #expr << " failed: 0x" << std::hex << _hr << std::dec \
                      << std::endl;                                                             \
            return _hr;                                                                         \
        }                                                                                       \
    } while (0)

namespace goldfish::videobridge {
namespace {

HRESULT CreateVideoMediaType(const GUID& subtype, int width, int height, int framerate,
                             ComPtr<IMFMediaType>& out_type) {
    RETURN_IF_FAILED(MFCreateMediaType(&out_type));
    RETURN_IF_FAILED(out_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    RETURN_IF_FAILED(out_type->SetGUID(MF_MT_SUBTYPE, subtype));
    RETURN_IF_FAILED(MFSetAttributeSize(out_type.Get(), MF_MT_FRAME_SIZE, width, height));
    RETURN_IF_FAILED(MFSetAttributeRatio(out_type.Get(), MF_MT_FRAME_RATE, framerate, 1));
    RETURN_IF_FAILED(out_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    return MFSetAttributeRatio(out_type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
}

HRESULT AllocateSampleBuffer(DWORD buffer_size, ComPtr<IMFSample>& out_sample,
                             ComPtr<IMFMediaBuffer>& out_buffer) {
    RETURN_IF_FAILED(MFCreateSample(&out_sample));
    RETURN_IF_FAILED(MFCreateMemoryBuffer(buffer_size, &out_buffer));
    return out_sample->AddBuffer(out_buffer.Get());
}

}  // namespace

MFVideoEncoderH264::MFVideoEncoderH264(webrtc::SdpVideoFormat format)
        : format_(std::move(format))
        , initialized_(false)
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
        ReleaseUnlocked();
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

int32_t MFVideoEncoderH264::InitEncode(const webrtc::VideoCodec* codec_settings,
                                       const webrtc::VideoEncoder::Settings& settings) {
    return InitEncode(codec_settings, settings.number_of_cores, settings.max_payload_size);
}

int32_t MFVideoEncoderH264::RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback) {
    absl::MutexLock lock(&mutex_);
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t MFVideoEncoderH264::Release() {
    absl::MutexLock lock(&mutex_);
    return ReleaseUnlocked();
}

int32_t MFVideoEncoderH264::ReleaseUnlocked() {
    if (!initialized_) {
        return WEBRTC_VIDEO_CODEC_OK;
    }

    if (encoder_mft_) {
        encoder_mft_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
        encoder_mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        encoder_mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        if (is_async_mft_) {
            ComPtr<IMFShutdown> shutdown;
            if (SUCCEEDED(encoder_mft_->QueryInterface(IID_PPV_ARGS(&shutdown)))) {
                shutdown->Shutdown();
            }
        }
    }

    input_sample_.Reset();
    input_buffer_.Reset();
    output_sample_.Reset();
    output_buffer_.Reset();
    input_type_.Reset();
    output_type_.Reset();
    event_generator_.Reset();
    encoder_mft_.Reset();
    is_async_mft_ = false;
    pending_input_count_ = 0;

    initialized_ = false;
    return WEBRTC_VIDEO_CODEC_OK;
}

int32_t MFVideoEncoderH264::Encode(const webrtc::VideoFrame& frame,
                                   const std::vector<webrtc::VideoFrameType>* frame_types) {
    absl::MutexLock lock(&mutex_);
    if (!initialized_ || !callback_) {
        return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }

    bool force_keyframe = false;
    if (frame_types) {
        for (const auto& type : *frame_types) {
            if (type == webrtc::VideoFrameType::kVideoFrameKey) {
                force_keyframe = true;
                break;
            }
        }
    }

    if (force_keyframe && encoder_mft_) {
        ComPtr<ICodecAPI> codec_api;
        if (SUCCEEDED(encoder_mft_->QueryInterface(IID_PPV_ARGS(&codec_api)))) {
            VARIANT var = {};
            var.vt = VT_UI4;
            var.ulVal = 1;
            codec_api->SetValue(&CODECAPI_AVEncVideoForceKeyFrame, &var);
        }
    }

    // 1. Process the input frame (convert to NV12 and feed to MFT)
    HRESULT hr = ProcessInputFrame(frame, force_keyframe);
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

    if (new_bitrate_bps != target_bitrate_bps_ || new_fps != target_fps_) {
        target_bitrate_bps_ = new_bitrate_bps;
        if (new_fps > 0) {
            target_fps_ = new_fps;
        }
        SetBitrateAndRateControl(target_bitrate_bps_);
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
    return S_OK;
}

HRESULT MFVideoEncoderH264::ConfigureMFT() {
    MFT_REGISTER_TYPE_INFO input_info = {MFMediaType_Video, MFVideoFormat_NV12};
    MFT_REGISTER_TYPE_INFO output_info = {MFMediaType_Video, MFVideoFormat_H264};

    IMFActivate** ppActivate = nullptr;
    UINT32 count = 0;

    // Enumerate hardware H.264 video encoders
    HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
                           MFT_ENUM_FLAG_HARDWARE,  // Force hardware acceleration
                           &input_info, &output_info, &ppActivate, &count);

    if (FAILED(hr) || count == 0) {
        RTC_LOG(LS_WARNING) << "No hardware H.264 MFT found. Falling back to software MFT.";
        hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT,
                       &input_info, &output_info, &ppActivate, &count);
    }

    if (FAILED(hr) || count == 0) {
        RTC_LOG(LS_ERROR) << "No H.264 MFT found: 0x" << std::hex << hr;
        return FAILED(hr) ? hr : E_FAIL;
    }

    HRESULT config_hr = E_FAIL;
    for (UINT32 i = 0; i < count; ++i) {
        encoder_mft_.Reset();
        hr = ppActivate[i]->ActivateObject(IID_PPV_ARGS(&encoder_mft_));
        if (FAILED(hr) || !encoder_mft_) {
            RTC_LOG(LS_WARNING) << "Failed to activate MFT candidate " << i << ": 0x" << std::hex
                                << hr;
            continue;
        }

        // Unlock asynchronous MFT if required (e.g. Intel Quick Sync Video MFT)
        ComPtr<IMFAttributes> attributes;
        is_async_mft_ = false;
        if (SUCCEEDED(encoder_mft_->GetAttributes(&attributes))) {
            UINT32 is_async = 0;
            if (SUCCEEDED(attributes->GetUINT32(MF_TRANSFORM_ASYNC, &is_async)) && is_async) {
                RTC_LOG(LS_INFO) << "MFT candidate " << i
                                 << " is async; setting MF_TRANSFORM_ASYNC_UNLOCK.";
                attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
                is_async_mft_ = true;
            }
        }

        if (is_async_mft_) {
            encoder_mft_->QueryInterface(IID_PPV_ARGS(&event_generator_));
        }

        // Query stream IDs
        DWORD input_count = 0;
        DWORD output_count = 0;
        hr = encoder_mft_->GetStreamCount(&input_count, &output_count);
        if (FAILED(hr) || input_count == 0 || output_count == 0) {
            RTC_LOG(LS_WARNING) << "GetStreamCount failed on candidate " << i << ": 0x" << std::hex
                                << hr;
            continue;
        }

        input_stream_id_ = 0;
        output_stream_id_ = 0;
        std::vector<DWORD> input_ids(input_count);
        std::vector<DWORD> output_ids(output_count);
        if (SUCCEEDED(encoder_mft_->GetStreamIDs(input_count, input_ids.data(), output_count,
                                                 output_ids.data()))) {
            input_stream_id_ = input_ids[0];
            output_stream_id_ = output_ids[0];
        }

        config_hr = ConfigureInputOutputTypes(codec_settings_.width, codec_settings_.height,
                                              target_fps_);
        if (SUCCEEDED(config_hr)) {
            RTC_LOG(LS_INFO) << "Successfully configured MFT candidate " << i;
            break;
        }
        RTC_LOG(LS_WARNING) << "ConfigureInputOutputTypes failed on candidate " << i << ": 0x"
                            << std::hex << config_hr;
    }

    for (UINT32 i = 0; i < count; ++i) {
        ppActivate[i]->Release();
    }
    CoTaskMemFree(ppActivate);

    if (FAILED(config_hr)) {
        RTC_LOG(LS_ERROR) << "All " << count << " MFT candidates failed configuration: 0x"
                          << std::hex << config_hr;
        return config_hr;
    }

    SetBitrateAndRateControl(target_bitrate_bps_);

    pending_input_count_ = 0;
    encoder_mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    encoder_mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    return S_OK;
}

HRESULT MFVideoEncoderH264::ConfigureInputOutputTypes(int width, int height, int framerate) {
    // 1. Configure Output Type (H.264)
    RETURN_IF_FAILED(
            CreateVideoMediaType(MFVideoFormat_H264, width, height, framerate, output_type_));
    RETURN_IF_FAILED(output_type_->SetUINT32(MF_MT_AVG_BITRATE, target_bitrate_bps_));

    // Configure profile (Baseline, Main, High / Constrained High)
    UINT32 profile = eAVEncH264VProfile_Base;
    auto profile_level_id = webrtc::ParseSdpForH264ProfileLevelId(format_.parameters);
    if (profile_level_id) {
        if (profile_level_id->profile == webrtc::H264Profile::kProfileHigh ||
            profile_level_id->profile == webrtc::H264Profile::kProfileConstrainedHigh) {
            profile = eAVEncH264VProfile_High;
        } else if (profile_level_id->profile == webrtc::H264Profile::kProfileMain) {
            profile = eAVEncH264VProfile_Main;
        }
    }
    RETURN_IF_FAILED(output_type_->SetUINT32(MF_MT_MPEG2_PROFILE, profile));

    RETURN_IF_FAILED(encoder_mft_->SetOutputType(output_stream_id_, output_type_.Get(), 0));

    // 2. Configure Input Type (NV12)
    RETURN_IF_FAILED(
            CreateVideoMediaType(MFVideoFormat_NV12, width, height, framerate, input_type_));
    RETURN_IF_FAILED(input_type_->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));

    RETURN_IF_FAILED(encoder_mft_->SetInputType(input_stream_id_, input_type_.Get(), 0));

    // Query output stream info (e.g. required buffer size)
    RETURN_IF_FAILED(encoder_mft_->GetOutputStreamInfo(output_stream_id_, &output_stream_info_));

    return S_OK;
}

HRESULT MFVideoEncoderH264::SetBitrateAndRateControl(uint32_t bitrate_bps) {
    ComPtr<ICodecAPI> codec_api;
    if (FAILED(encoder_mft_->QueryInterface(IID_PPV_ARGS(&codec_api)))) {
        return S_OK;
    }

    // Set Rate Control Mode (Constant Bitrate)
    VARIANT var = {};
    var.vt = VT_UI4;
    var.ulVal = eAVEncCommonRateControlMode_CBR;
    codec_api->SetValue(&CODECAPI_AVEncCommonRateControlMode, &var);

    // Set Average Bitrate
    var.ulVal = bitrate_bps;
    codec_api->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &var);

    // Set GOP size (Keyframe interval) to 2 seconds (2 * FPS)
    var.ulVal = target_fps_ * 2;
    codec_api->SetValue(&CODECAPI_AVEncMPVGOPSize, &var);

    // Enable Low Latency mode for real-time WebRTC streaming
    VARIANT var_ll = {};
    var_ll.vt = VT_BOOL;
    var_ll.boolVal = VARIANT_TRUE;
    codec_api->SetValue(&CODECAPI_AVEncCommonLowLatency, &var_ll);

    // Disable B-frames for zero-latency interactive streaming
    var.ulVal = 0;
    codec_api->SetValue(&CODECAPI_AVEncMPVDefaultBPictureCount, &var);

    return S_OK;
}

HRESULT MFVideoEncoderH264::AllocateInputBuffers(int width, int height) {
    return AllocateSampleBuffer(width * height * 3 / 2, input_sample_, input_buffer_);
}

HRESULT MFVideoEncoderH264::AllocateOutputBuffers() {
    DWORD buffer_size = output_stream_info_.cbSize;
    if (buffer_size == 0) {
        const DWORD nv12_size = codec_settings_.width * codec_settings_.height * 3 / 2;
        buffer_size = (std::max)(nv12_size, DWORD{1048576});
    }
    return AllocateSampleBuffer(buffer_size, output_sample_, output_buffer_);
}

HRESULT MFVideoEncoderH264::ProcessInputFrame(const webrtc::VideoFrame& frame,
                                              bool force_keyframe) {
    webrtc::scoped_refptr<webrtc::I420BufferInterface> i420_buffer =
            frame.video_frame_buffer()->ToI420();

    int width = i420_buffer->width();
    int height = i420_buffer->height();
    DWORD buffer_size = width * height * 3 / 2;

    ComPtr<IMFSample> sample;
    ComPtr<IMFMediaBuffer> media_buffer;
    RETURN_IF_FAILED(AllocateSampleBuffer(buffer_size, sample, media_buffer));

    BYTE* buffer_data = nullptr;
    DWORD max_length = 0;
    DWORD current_length = 0;
    RETURN_IF_FAILED(media_buffer->Lock(&buffer_data, &max_length, &current_length));

    uint8_t* dst_y = buffer_data;
    int dst_stride_y = width;
    uint8_t* dst_uv = buffer_data + (width * height);
    int dst_stride_uv = width;

    // Convert I420 -> NV12 using libyuv
    libyuv::I420ToNV12(i420_buffer->DataY(), i420_buffer->StrideY(), i420_buffer->DataU(),
                       i420_buffer->StrideU(), i420_buffer->DataV(), i420_buffer->StrideV(), dst_y,
                       dst_stride_y, dst_uv, dst_stride_uv, width, height);

    media_buffer->Unlock();
    RETURN_IF_FAILED(media_buffer->SetCurrentLength(buffer_size));

    if (force_keyframe) {
        RETURN_IF_FAILED(sample->SetUINT32(MFSampleExtension_CleanPoint, 1));
    }

    // Set timestamp and duration on the sample (in 100-nanosecond units)
    RETURN_IF_FAILED(sample->SetSampleTime(frame.timestamp_us() * 10));
    UINT64 duration = 10000000ULL / (target_fps_ ? target_fps_ : 30);
    RETURN_IF_FAILED(sample->SetSampleDuration(duration));

    // If this is an asynchronous MFT, we must follow the IMFMediaEventGenerator event loop:
    // Wait for METransformNeedInput before calling ProcessInput, and drain on
    // METransformHaveOutput.
    if (is_async_mft_ && event_generator_) {
        while (pending_input_count_ == 0) {
            ComPtr<IMFMediaEvent> event;
            HRESULT ev_hr = event_generator_->GetEvent(0, &event);
            if (FAILED(ev_hr)) {
                return ev_hr;
            }
            MediaEventType event_type = MEUnknown;
            event->GetType(&event_type);
            if (event_type == METransformNeedInput) {
                pending_input_count_++;
            } else if (event_type == METransformHaveOutput) {
                RETURN_IF_FAILED(DrainEncodedFrames(frame.rtp_timestamp(), frame.render_time_ms(),
                                                    callback_));
            }
        }
        pending_input_count_--;
        return encoder_mft_->ProcessInput(input_stream_id_, sample.Get(), 0);
    }

    // Synchronous MFT path
    HRESULT hr = encoder_mft_->ProcessInput(input_stream_id_, sample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING) {
        DrainEncodedFrames(frame.rtp_timestamp(), frame.render_time_ms(), callback_);
        hr = encoder_mft_->ProcessInput(input_stream_id_, sample.Get(), 0);
    }
    return hr;
}

HRESULT MFVideoEncoderH264::DrainEncodedFrames(uint32_t rtp_timestamp, int64_t capture_time_ms,
                                               webrtc::EncodedImageCallback* callback) {
    bool mft_provides_samples =
            (output_stream_info_.dwFlags &
             (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;

    bool drained_any = false;
    while (true) {
        if (is_async_mft_ && event_generator_) {
            bool have_output = false;
            while (true) {
                ComPtr<IMFMediaEvent> event;
                // Wait blocking for the first output event; once at least one output frame has been
                // drained (or if the MFT asks for more input), poll non-blocking for any remaining
                // events.
                DWORD flags = drained_any ? MF_EVENT_FLAG_NO_WAIT : 0;
                HRESULT ev_hr = event_generator_->GetEvent(flags, &event);
                if (ev_hr == MF_E_NO_EVENTS_AVAILABLE || FAILED(ev_hr)) {
                    break;
                }
                MediaEventType event_type = MEUnknown;
                event->GetType(&event_type);
                if (event_type == METransformNeedInput) {
                    pending_input_count_++;
                    // Once the MFT signals NeedInput, switch to non-blocking check to see if
                    // an output event was also queued; if not, return so Encode() can finish.
                    drained_any = true;
                } else if (event_type == METransformHaveOutput) {
                    have_output = true;
                    drained_any = true;
                    break;
                }
            }
            if (!have_output) {
                break;
            }
        }

        // Reset the output buffer length before requesting new output from MFT
        if (!mft_provides_samples && output_buffer_) {
            output_buffer_->SetCurrentLength(0);
        }

        MFT_OUTPUT_DATA_BUFFER output_data = {};
        output_data.dwStreamID = output_stream_id_;
        output_data.pSample = mft_provides_samples ? nullptr : output_sample_.Get();

        DWORD status = 0;
        HRESULT hr = encoder_mft_->ProcessOutput(0, 1, &output_data, &status);
        if (output_data.pEvents) {
            output_data.pEvents->Release();
        }

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            break;
        }
        if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
            ComPtr<IMFMediaType> new_output_type;
            if (SUCCEEDED(encoder_mft_->GetOutputAvailableType(output_stream_id_, 0,
                                                               &new_output_type))) {
                encoder_mft_->SetOutputType(output_stream_id_, new_output_type.Get(), 0);
                encoder_mft_->GetOutputStreamInfo(output_stream_id_, &output_stream_info_);
                mft_provides_samples = (output_stream_info_.dwFlags &
                                        (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES |
                                         MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
            }
            continue;
        }

        if (FAILED(hr)) {
            RTC_LOG(LS_ERROR) << "MFT ProcessOutput failed: 0x" << std::hex << hr;
            return hr;
        }

        IMFSample* result_sample =
                mft_provides_samples ? output_data.pSample : output_sample_.Get();
        if (!result_sample) {
            continue;
        }

        ComPtr<IMFMediaBuffer> result_buffer;
        hr = result_sample->ConvertToContiguousBuffer(&result_buffer);
        if (FAILED(hr) || !result_buffer) {
            if (mft_provides_samples && output_data.pSample) {
                output_data.pSample->Release();
            }
            return FAILED(hr) ? hr : E_FAIL;
        }

        // Retrieve the encoded payload
        BYTE* data = nullptr;
        DWORD current_length = 0;
        hr = result_buffer->Lock(&data, nullptr, &current_length);
        if (FAILED(hr)) {
            if (mft_provides_samples && output_data.pSample) {
                output_data.pSample->Release();
            }
            return hr;
        }

        // Check if this is a keyframe (look for H.264 SPS/PPS or Keyframe attribute)
        UINT32 is_keyframe = 0;
        result_sample->GetUINT32(MFSampleExtension_CleanPoint, &is_keyframe);

        // Inspect bitstream for IDR or SPS NAL units to ensure keyframe classification is accurate
        if (data && current_length >= 4) {
            for (DWORD i = 0; i + 4 <= current_length; ++i) {
                if (data[i] != 0 || data[i + 1] != 0) continue;
                const DWORD nalu_idx = (data[i + 2] == 1) ? i + 3
                                       : (i + 4 < current_length && data[i + 2] == 0 &&
                                          data[i + 3] == 1)
                                               ? i + 4
                                               : 0;
                const uint8_t nal_type = nalu_idx ? (data[nalu_idx] & 0x1F) : 0;
                if (nal_type == 5 || nal_type == 7) {
                    is_keyframe = 1;
                    break;
                }
            }
        }

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
        codec_info.codecSpecific.H264.packetization_mode =
                webrtc::H264PacketizationMode::NonInterleaved;

        result_buffer->Unlock();
        if (mft_provides_samples && output_data.pSample) {
            output_data.pSample->Release();
        }

        // Deliver the encoded image
        if (callback) {
            callback->OnEncodedImage(encoded_image, &codec_info);
        }
    }

    return S_OK;
}

}  // namespace goldfish::videobridge
