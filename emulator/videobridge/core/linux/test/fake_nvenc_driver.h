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
#pragma once

#include <nvEncodeAPI.h>

#include <cstdint>
#include <vector>

namespace goldfish::videobridge {

/**
 * @class FakeNvencDriver
 * @brief In-memory mock implementation of the NVIDIA NVENC API for hermetic unit testing.
 *
 * Implements the function pointer table defined by @c NV_ENCODE_API_FUNCTION_LIST.
 * Generates realistic Annex B H.264 NAL units (SPS, PPS, IDR, non-IDR) and tracks
 * session parameters, buffer allocations, rate reconfigurations, and frame submissions.
 */
class FakeNvencDriver {
  public:
    FakeNvencDriver();
    ~FakeNvencDriver();

    NV_ENCODE_API_FUNCTION_LIST CreateFunctionList();

    // Test inspection queries
    int encode_calls() const { return encode_calls_; }
    int force_idr_calls() const { return force_idr_calls_; }
    int reconfigure_calls() const { return reconfigure_calls_; }
    uint32_t last_bitrate() const { return last_bitrate_; }
    uint32_t last_framerate() const { return last_framerate_; }
    const GUID& last_profile_guid() const { return last_profile_guid_; }
    bool session_active() const { return session_active_; }

    void Reset();

  private:
    static NVENCSTATUS NVENCAPI OpenEncodeSessionEx(NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS* params,
                                                    void** encoder);
    static NVENCSTATUS NVENCAPI GetEncodeGUIDCount(void* encoder, uint32_t* guid_count);
    static NVENCSTATUS NVENCAPI GetEncodeGUIDs(void* encoder, GUID* guids, uint32_t guid_array_size,
                                               uint32_t* guid_count);
    static NVENCSTATUS NVENCAPI GetEncodeProfileGUIDCount(void* encoder, GUID encode_guid,
                                                          uint32_t* count);
    static NVENCSTATUS NVENCAPI GetEncodeProfileGUIDs(void* encoder, GUID encode_guid,
                                                      GUID* profile_guids, uint32_t guid_array_size,
                                                      uint32_t* count);
    static NVENCSTATUS NVENCAPI GetInputFormatCount(void* encoder, GUID encode_guid,
                                                    uint32_t* count);
    static NVENCSTATUS NVENCAPI GetInputFormats(void* encoder, GUID encode_guid,
                                                NV_ENC_BUFFER_FORMAT* formats,
                                                uint32_t buffer_fmt_array_size, uint32_t* count);
    static NVENCSTATUS NVENCAPI GetEncodePresetConfig(void* encoder, GUID encode_guid,
                                                      GUID preset_guid,
                                                      NV_ENC_PRESET_CONFIG* preset_config);
    static NVENCSTATUS NVENCAPI InitializeEncoder(void* encoder,
                                                  NV_ENC_INITIALIZE_PARAMS* create_encode_params);
    static NVENCSTATUS NVENCAPI
    CreateInputBuffer(void* encoder, NV_ENC_CREATE_INPUT_BUFFER* create_input_buffer_params);
    static NVENCSTATUS NVENCAPI DestroyInputBuffer(void* encoder, NV_ENC_INPUT_PTR input_buffer);
    static NVENCSTATUS NVENCAPI CreateBitstreamBuffer(
            void* encoder, NV_ENC_CREATE_BITSTREAM_BUFFER* create_bitstream_buffer_params);
    static NVENCSTATUS NVENCAPI DestroyBitstreamBuffer(void* encoder,
                                                       NV_ENC_OUTPUT_PTR bitstream_buffer);
    static NVENCSTATUS NVENCAPI LockInputBuffer(void* encoder,
                                                NV_ENC_LOCK_INPUT_BUFFER* lock_input_buffer_params);
    static NVENCSTATUS NVENCAPI UnlockInputBuffer(void* encoder, NV_ENC_INPUT_PTR input_buffer);
    static NVENCSTATUS NVENCAPI LockBitstream(void* encoder,
                                              NV_ENC_LOCK_BITSTREAM* lock_bitstream_buffer_params);
    static NVENCSTATUS NVENCAPI UnlockBitstream(void* encoder, NV_ENC_OUTPUT_PTR bitstream_buffer);
    static NVENCSTATUS NVENCAPI EncodePicture(void* encoder, NV_ENC_PIC_PARAMS* encode_pic_params);
    static NVENCSTATUS NVENCAPI ReconfigureEncoder(void* encoder,
                                                   NV_ENC_RECONFIGURE_PARAMS* reconfigure_params);
    static NVENCSTATUS NVENCAPI DestroyEncoder(void* encoder);

    static FakeNvencDriver* instance_;

    bool session_active_{false};
    int encode_calls_{0};
    int force_idr_calls_{0};
    int reconfigure_calls_{0};
    uint32_t last_bitrate_{0};
    uint32_t last_framerate_{0};
    GUID last_profile_guid_{};
    bool last_frame_was_idr_{false};

    std::vector<uint8_t> input_buffer_data_;
    std::vector<uint8_t> idr_bitstream_payload_;
    std::vector<uint8_t> delta_bitstream_payload_;
};

}  // namespace goldfish::videobridge
