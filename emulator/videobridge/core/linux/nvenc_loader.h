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
#include <memory>
#include <string>

#include "absl/status/statusor.h"

#include "goldfish/os/dynamic_library.h"

namespace goldfish::videobridge {

/**
 * @brief Converts an NVIDIA NVENC status code into a human- and AI-readable diagnostic string.
 */
std::string NvencStatusToString(NVENCSTATUS status);

template <typename T>
struct NvencStructVersion;

template <>
struct NvencStructVersion<NV_ENCODE_API_FUNCTION_LIST> {
    static constexpr uint32_t value = NV_ENCODE_API_FUNCTION_LIST_VER;
};

template <>
struct NvencStructVersion<NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS> {
    static constexpr uint32_t value = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
};

template <>
struct NvencStructVersion<NV_ENC_PRESET_CONFIG> {
    static constexpr uint32_t value = NV_ENC_PRESET_CONFIG_VER;
};

template <>
struct NvencStructVersion<NV_ENC_INITIALIZE_PARAMS> {
    static constexpr uint32_t value = NV_ENC_INITIALIZE_PARAMS_VER;
};

template <>
struct NvencStructVersion<NV_ENC_CONFIG> {
    static constexpr uint32_t value = NV_ENC_CONFIG_VER;
};

template <>
struct NvencStructVersion<NV_ENC_CREATE_INPUT_BUFFER> {
    static constexpr uint32_t value = NV_ENC_CREATE_INPUT_BUFFER_VER;
};

template <>
struct NvencStructVersion<NV_ENC_CREATE_BITSTREAM_BUFFER> {
    static constexpr uint32_t value = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
};

template <>
struct NvencStructVersion<NV_ENC_LOCK_INPUT_BUFFER> {
    static constexpr uint32_t value = NV_ENC_LOCK_INPUT_BUFFER_VER;
};

template <>
struct NvencStructVersion<NV_ENC_PIC_PARAMS> {
    static constexpr uint32_t value = NV_ENC_PIC_PARAMS_VER;
};

template <>
struct NvencStructVersion<NV_ENC_LOCK_BITSTREAM> {
    static constexpr uint32_t value = NV_ENC_LOCK_BITSTREAM_VER;
};

template <>
struct NvencStructVersion<NV_ENC_RECONFIGURE_PARAMS> {
    static constexpr uint32_t value = NV_ENC_RECONFIGURE_PARAMS_VER;
};

/**
 * @brief Type-safe factory that zero-initializes an NVENC C-API structure and sets its version
 * field.
 */
template <typename T>
[[nodiscard]] inline T MakeNvencStruct() {
    T s{};
    s.version = NvencStructVersion<T>::value;
    return s;
}

/**
 * @class NvencLoader
 * @brief Dynamic loader and symbol resolver for NVIDIA NVENC video encoding library.
 *
 * Encapsulates the dynamic loading of `libnvidia-encode.so.1` using RAII-managed
 * `goldfish::os::DynamicLibrary` and populates the `NV_ENCODE_API_FUNCTION_LIST`
 * function pointer table via `NvEncodeAPICreateInstance()`.
 */
class NvencLoader {
  public:
    static absl::StatusOr<std::unique_ptr<NvencLoader>> Create(
            const std::string& custom_library_path = "");

    static absl::StatusOr<std::unique_ptr<NvencLoader>> CreateForTest(
            const NV_ENCODE_API_FUNCTION_LIST& function_list);

    explicit NvencLoader(goldfish::os::DynamicLibrary lib,
                         const NV_ENCODE_API_FUNCTION_LIST& fn_list);
    explicit NvencLoader(const NV_ENCODE_API_FUNCTION_LIST& fn_list);
    ~NvencLoader() = default;

    // Move-only
    NvencLoader(NvencLoader&&) noexcept = default;
    NvencLoader& operator=(NvencLoader&&) noexcept = default;
    NvencLoader(const NvencLoader&) = delete;
    NvencLoader& operator=(const NvencLoader&) = delete;

    const NV_ENCODE_API_FUNCTION_LIST& api() const { return fn_list_; }

  private:
    goldfish::os::DynamicLibrary lib_;
    NV_ENCODE_API_FUNCTION_LIST fn_list_{};
};

}  // namespace goldfish::videobridge
