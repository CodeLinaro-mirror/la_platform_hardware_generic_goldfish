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
#include "core/linux/test/fake_nvenc_driver.h"

#include <cstddef>
#include <cstring>

namespace goldfish::videobridge {

FakeNvencDriver* FakeNvencDriver::instance_ = nullptr;

FakeNvencDriver::FakeNvencDriver() {
    instance_ = this;
    Reset();

    // Construct synthetic H.264 Annex B bitstreams
    // IDR Payload: SPS (NALU 7) + PPS (NALU 8) + IDR Slice (NALU 5)
    idr_bitstream_payload_ = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x1f, 0xda, 0x01, 0x40, 0x16,  // SPS
        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x38, 0x80,                          // PPS
        0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x00, 0x10, 0x20, 0x30         // IDR Slice
    };

    // Delta Payload: Non-IDR Slice (NALU 1)
    delta_bitstream_payload_ = {
        0x00, 0x00, 0x00, 0x01, 0x41, 0x9a, 0x00, 0x10, 0x20, 0x30  // P Slice
    };

    input_buffer_data_.resize(static_cast<size_t>(1920) * 1080 * 2, 0);
}

FakeNvencDriver::~FakeNvencDriver() {
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

void FakeNvencDriver::Reset() {
    session_active_ = false;
    encode_calls_ = 0;
    force_idr_calls_ = 0;
    reconfigure_calls_ = 0;
    last_bitrate_ = 0;
    last_framerate_ = 0;
    last_profile_guid_ = {};
    last_frame_was_idr_ = false;
}

NV_ENCODE_API_FUNCTION_LIST FakeNvencDriver::CreateFunctionList() {
    NV_ENCODE_API_FUNCTION_LIST list{};
    list.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    list.nvEncOpenEncodeSessionEx = &FakeNvencDriver::OpenEncodeSessionEx;
    list.nvEncGetEncodeGUIDCount = &FakeNvencDriver::GetEncodeGUIDCount;
    list.nvEncGetEncodeGUIDs = &FakeNvencDriver::GetEncodeGUIDs;
    list.nvEncGetEncodeProfileGUIDCount = &FakeNvencDriver::GetEncodeProfileGUIDCount;
    list.nvEncGetEncodeProfileGUIDs = &FakeNvencDriver::GetEncodeProfileGUIDs;
    list.nvEncGetInputFormatCount = &FakeNvencDriver::GetInputFormatCount;
    list.nvEncGetInputFormats = &FakeNvencDriver::GetInputFormats;
    list.nvEncGetEncodePresetConfig = &FakeNvencDriver::GetEncodePresetConfig;
    list.nvEncInitializeEncoder = &FakeNvencDriver::InitializeEncoder;
    list.nvEncCreateInputBuffer = &FakeNvencDriver::CreateInputBuffer;
    list.nvEncDestroyInputBuffer = &FakeNvencDriver::DestroyInputBuffer;
    list.nvEncCreateBitstreamBuffer = &FakeNvencDriver::CreateBitstreamBuffer;
    list.nvEncDestroyBitstreamBuffer = &FakeNvencDriver::DestroyBitstreamBuffer;
    list.nvEncLockInputBuffer = &FakeNvencDriver::LockInputBuffer;
    list.nvEncUnlockInputBuffer = &FakeNvencDriver::UnlockInputBuffer;
    list.nvEncLockBitstream = &FakeNvencDriver::LockBitstream;
    list.nvEncUnlockBitstream = &FakeNvencDriver::UnlockBitstream;
    list.nvEncEncodePicture = &FakeNvencDriver::EncodePicture;
    list.nvEncReconfigureEncoder = &FakeNvencDriver::ReconfigureEncoder;
    list.nvEncDestroyEncoder = &FakeNvencDriver::DestroyEncoder;
    return list;
}

NVENCSTATUS NVENCAPI
FakeNvencDriver::OpenEncodeSessionEx(NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS* params, void** encoder) {
    if (!encoder || !params || !instance_) return NV_ENC_ERR_INVALID_PTR;
    instance_->session_active_ = true;
    *encoder = static_cast<void*>(instance_);
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI FakeNvencDriver::GetEncodeGUIDCount(void* /*encoder*/, uint32_t* guid_count) {
    if (!guid_count) return NV_ENC_ERR_INVALID_PTR;
    *guid_count = 1;
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI FakeNvencDriver::GetEncodeGUIDs(void* /*encoder*/, GUID* guids,
                                                     uint32_t guid_array_size,
                                                     uint32_t* guid_count) {
    if (!guids || !guid_count) return NV_ENC_ERR_INVALID_PTR;
    if (guid_array_size > 0) {
        guids[0] = NV_ENC_CODEC_H264_GUID;
        *guid_count = 1;
    }
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI FakeNvencDriver::GetEncodeProfileGUIDCount(void* /*encoder*/,
                                                                GUID /*encode_guid*/,
                                                                uint32_t* count) {
    if (!count) return NV_ENC_ERR_INVALID_PTR;
    *count = 3;
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI FakeNvencDriver::GetEncodeProfileGUIDs(void* /*encoder*/, GUID /*encode_guid*/,
                                                            GUID* profile_guids,
                                                            uint32_t guid_array_size,
                                                            uint32_t* count) {
    if (!profile_guids || !count) return NV_ENC_ERR_INVALID_PTR;
    uint32_t n = 0;
    if (n < guid_array_size) profile_guids[n++] = NV_ENC_H264_PROFILE_BASELINE_GUID;
    if (n < guid_array_size) profile_guids[n++] = NV_ENC_H264_PROFILE_MAIN_GUID;
    if (n < guid_array_size) profile_guids[n++] = NV_ENC_H264_PROFILE_HIGH_GUID;
    *count = n;
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI FakeNvencDriver::GetInputFormatCount(void* /*encoder*/, GUID /*encode_guid*/,
                                                          uint32_t* count) {
    if (!count) return NV_ENC_ERR_INVALID_PTR;
    *count = 1;
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI FakeNvencDriver::GetInputFormats(void* /*encoder*/, GUID /*encode_guid*/,
                                                      NV_ENC_BUFFER_FORMAT* formats,
                                                      uint32_t buffer_fmt_array_size,
                                                      uint32_t* count) {
    if (!formats || !count) return NV_ENC_ERR_INVALID_PTR;
    if (buffer_fmt_array_size > 0) {
        formats[0] = NV_ENC_BUFFER_FORMAT_NV12;
        *count = 1;
    }
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI FakeNvencDriver::GetEncodePresetConfig(void* /*encoder*/, GUID /*encode_guid*/,
                                                            GUID /*preset_guid*/,
                                                            NV_ENC_PRESET_CONFIG* preset_config) {
    if (!preset_config) return NV_ENC_ERR_INVALID_PTR;
    preset_config->presetCfg.version = NV_ENC_CONFIG_VER;
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI FakeNvencDriver::InitializeEncoder(
        void* /*encoder*/, NV_ENC_INITIALIZE_PARAMS* create_encode_params) {
    if (!create_encode_params) return NV_ENC_ERR_INVALID_PTR;
    if (instance_ && create_encode_params->encodeConfig) {
        instance_->last_profile_guid_ = create_encode_params->encodeConfig->profileGUID;
    }
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI FakeNvencDriver::CreateInputBuffer(
        void* /*encoder*/, NV_ENC_CREATE_INPUT_BUFFER* create_input_buffer_params) {
    if (!create_input_buffer_params) return NV_ENC_ERR_INVALID_PTR;
    create_input_buffer_params->inputBuffer = reinterpret_cast<NV_ENC_INPUT_PTR>(0xABCD0001);
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI FakeNvencDriver::DestroyInputBuffer(void* /*encoder*/,
                                                         NV_ENC_INPUT_PTR /*input_buffer*/) {
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI FakeNvencDriver::CreateBitstreamBuffer(
        void* /*encoder*/, NV_ENC_CREATE_BITSTREAM_BUFFER* create_bitstream_buffer_params) {
    if (!create_bitstream_buffer_params) return NV_ENC_ERR_INVALID_PTR;
    create_bitstream_buffer_params->bitstreamBuffer =
            reinterpret_cast<NV_ENC_OUTPUT_PTR>(0xDCBA0001);
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI
FakeNvencDriver::DestroyBitstreamBuffer(void* /*encoder*/, NV_ENC_OUTPUT_PTR /*bitstream_buffer*/) {
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI FakeNvencDriver::LockInputBuffer(
        void* encoder, NV_ENC_LOCK_INPUT_BUFFER* lock_input_buffer_params) {
    auto* self = static_cast<FakeNvencDriver*>(encoder);
    if (!lock_input_buffer_params || !self) return NV_ENC_ERR_INVALID_PTR;
    lock_input_buffer_params->bufferDataPtr = self->input_buffer_data_.data();
    lock_input_buffer_params->pitch = 1920;
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI FakeNvencDriver::UnlockInputBuffer(void* /*encoder*/,
                                                        NV_ENC_INPUT_PTR /*input_buffer*/) {
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI FakeNvencDriver::EncodePicture(void* encoder,
                                                    NV_ENC_PIC_PARAMS* encode_pic_params) {
    auto* self = static_cast<FakeNvencDriver*>(encoder);
    if (!encode_pic_params || !self) return NV_ENC_ERR_INVALID_PTR;
    self->encode_calls_++;
    if (encode_pic_params->encodePicFlags & NV_ENC_PIC_FLAG_FORCEIDR) {
        self->force_idr_calls_++;
        self->last_frame_was_idr_ = true;
    } else {
        self->last_frame_was_idr_ = false;
    }
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI
FakeNvencDriver::LockBitstream(void* encoder, NV_ENC_LOCK_BITSTREAM* lock_bitstream_buffer_params) {
    auto* self = static_cast<FakeNvencDriver*>(encoder);
    if (!lock_bitstream_buffer_params || !self) return NV_ENC_ERR_INVALID_PTR;
    if (self->last_frame_was_idr_) {
        lock_bitstream_buffer_params->bitstreamBufferPtr = self->idr_bitstream_payload_.data();
        lock_bitstream_buffer_params->bitstreamSizeInBytes = self->idr_bitstream_payload_.size();
        lock_bitstream_buffer_params->pictureType = NV_ENC_PIC_TYPE_IDR;
    } else {
        lock_bitstream_buffer_params->bitstreamBufferPtr = self->delta_bitstream_payload_.data();
        lock_bitstream_buffer_params->bitstreamSizeInBytes = self->delta_bitstream_payload_.size();
        lock_bitstream_buffer_params->pictureType = NV_ENC_PIC_TYPE_P;
    }
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI FakeNvencDriver::UnlockBitstream(void* /*encoder*/,
                                                      NV_ENC_OUTPUT_PTR /*bitstream_buffer*/) {
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI
FakeNvencDriver::ReconfigureEncoder(void* encoder, NV_ENC_RECONFIGURE_PARAMS* reconfigure_params) {
    auto* self = static_cast<FakeNvencDriver*>(encoder);
    if (!reconfigure_params || !self) return NV_ENC_ERR_INVALID_PTR;
    self->reconfigure_calls_++;
    if (reconfigure_params->reInitEncodeParams.encodeConfig) {
        self->last_bitrate_ =
                reconfigure_params->reInitEncodeParams.encodeConfig->rcParams.averageBitRate;
    }
    if (reconfigure_params->reInitEncodeParams.frameRateNum &&
        reconfigure_params->reInitEncodeParams.frameRateDen) {
        self->last_framerate_ = reconfigure_params->reInitEncodeParams.frameRateNum /
                                reconfigure_params->reInitEncodeParams.frameRateDen;
    }
    return NV_ENC_SUCCESS;
}

NVENCSTATUS NVENCAPI FakeNvencDriver::DestroyEncoder(void* encoder) {
    auto* self = static_cast<FakeNvencDriver*>(encoder);
    if (self) self->session_active_ = false;
    return NV_ENC_SUCCESS;
}

}  // namespace goldfish::videobridge
