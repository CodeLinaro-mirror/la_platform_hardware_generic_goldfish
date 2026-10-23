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
#include "core/linux/nvenc_loader.h"

#include <initializer_list>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

#include "android/base/system.h"

namespace goldfish::videobridge {

namespace {
using NvEncodeAPICreateInstanceFunc = NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*);
using CuInitFunc = int (*)(unsigned int);
using CuDeviceGetFunc = int (*)(int*, int);
using CuCtxCreateFunc = int (*)(void**, unsigned int, int);
constexpr int kCudaSuccess = 0;

std::vector<std::string> BuildCandidatePaths(const char* env_var,
                                             std::initializer_list<const char*> defaults) {
    std::vector<std::string> paths;
    if (std::string env_path = android::base::System::Get()->EnvGet(env_var); !env_path.empty()) {
        paths.push_back(std::move(env_path));
    }
    paths.insert(paths.end(), defaults.begin(), defaults.end());
    return paths;
}

template <typename Fn>
Fn LoadSymbol(const goldfish::os::DynamicLibrary& lib, const char* primary,
              const char* fallback = nullptr) {
    if (void* sym = lib[primary]) {
        return reinterpret_cast<Fn>(sym);
    }
    return fallback ? reinterpret_cast<Fn>(lib[fallback]) : nullptr;
}
}  // namespace

std::string NvencStatusToString(NVENCSTATUS status) {
    switch (status) {
    case NV_ENC_SUCCESS:
        return "NV_ENC_SUCCESS (0)";
    case NV_ENC_ERR_NO_ENCODE_DEVICE:
        return "NV_ENC_ERR_NO_ENCODE_DEVICE (1: No NVENC-capable GPU device found)";
    case NV_ENC_ERR_UNSUPPORTED_DEVICE:
        return "NV_ENC_ERR_UNSUPPORTED_DEVICE (2: Selected device does not support NVENC)";
    case NV_ENC_ERR_INVALID_ENCODERDEVICE:
        return "NV_ENC_ERR_INVALID_ENCODERDEVICE (3: Invalid encoder device handle)";
    case NV_ENC_ERR_INVALID_DEVICE:
        return "NV_ENC_ERR_INVALID_DEVICE (4: Device handle passed is invalid)";
    case NV_ENC_ERR_DEVICE_NOT_EXIST:
        return "NV_ENC_ERR_DEVICE_NOT_EXIST (5: Device passed no longer exists)";
    case NV_ENC_ERR_INVALID_PTR:
        return "NV_ENC_ERR_INVALID_PTR (6: Invalid pointer provided)";
    case NV_ENC_ERR_INVALID_EVENT:
        return "NV_ENC_ERR_INVALID_EVENT (7: Invalid event handle)";
    case NV_ENC_ERR_INVALID_PARAM:
        return "NV_ENC_ERR_INVALID_PARAM (8: Invalid parameter or unsupported config)";
    case NV_ENC_ERR_INVALID_CALL:
        return "NV_ENC_ERR_INVALID_CALL (9: Call not valid in current state)";
    case NV_ENC_ERR_OUT_OF_MEMORY:
        return "NV_ENC_ERR_OUT_OF_MEMORY (10: Out of memory or session quota exceeded)";
    case NV_ENC_ERR_ENCODER_NOT_INITIALIZED:
        return "NV_ENC_ERR_ENCODER_NOT_INITIALIZED (11: Encoder session not initialized)";
    case NV_ENC_ERR_UNSUPPORTED_PARAM:
        return "NV_ENC_ERR_UNSUPPORTED_PARAM (12: Unsupported parameter configuration)";
    case NV_ENC_ERR_LOCK_BUSY:
        return "NV_ENC_ERR_LOCK_BUSY (13: Hardware resource currently locked/busy)";
    case NV_ENC_ERR_NOT_ENOUGH_BUFFER:
        return "NV_ENC_ERR_NOT_ENOUGH_BUFFER (14: Buffer size insufficient)";
    case NV_ENC_ERR_INVALID_VERSION:
        return "NV_ENC_ERR_INVALID_VERSION (15: Struct version mismatch)";
    case NV_ENC_ERR_MAP_FAILED:
        return "NV_ENC_ERR_MAP_FAILED (16: Failed to map resource)";
    case NV_ENC_ERR_NEED_MORE_INPUT:
        return "NV_ENC_ERR_NEED_MORE_INPUT (17: Encoder requires more input frames)";
    case NV_ENC_ERR_ENCODER_BUSY:
        return "NV_ENC_ERR_ENCODER_BUSY (18: HW encoder busy, retry later)";
    case NV_ENC_ERR_EVENT_NOT_REGISTERD:
        return "NV_ENC_ERR_EVENT_NOT_REGISTERD (19: Event not registered)";
    case NV_ENC_ERR_GENERIC:
        return "NV_ENC_ERR_GENERIC (20: Unspecified internal error)";
    case NV_ENC_ERR_INCOMPATIBLE_CLIENT_KEY:
        return "NV_ENC_ERR_INCOMPATIBLE_CLIENT_KEY (21: Client key incompatible)";
    case NV_ENC_ERR_UNIMPLEMENTED:
        return "NV_ENC_ERR_UNIMPLEMENTED (22: Feature not implemented)";
    case NV_ENC_ERR_RESOURCE_REGISTER_FAILED:
        return "NV_ENC_ERR_RESOURCE_REGISTER_FAILED (23: Resource registration failed)";
    case NV_ENC_ERR_RESOURCE_NOT_REGISTERED:
        return "NV_ENC_ERR_RESOURCE_NOT_REGISTERED (24: Resource not registered)";
    case NV_ENC_ERR_RESOURCE_NOT_MAPPED:
        return "NV_ENC_ERR_RESOURCE_NOT_MAPPED (25: Resource not mapped)";
    default:
        return absl::StrCat("NVENC_UNKNOWN_ERROR (", static_cast<int>(status), ")");
    }
}

absl::StatusOr<std::unique_ptr<NvencLoader>> NvencLoader::Create(
        const std::string& custom_library_path) {
    const std::vector<std::string> candidate_paths =
            !custom_library_path.empty()
                    ? std::vector<std::string>{custom_library_path}
                    : BuildCandidatePaths("NVENC_LIB_PATH",
                                          {
                                              "libnvidia-encode.so.1",
                                              "libnvidia-encode.so",
                                              "/usr/local/nvidia/lib64/libnvidia-encode.so.1",
                                              "/usr/lib/x86_64-linux-gnu/libnvidia-encode.so.1",
                                              "/usr/lib64/libnvidia-encode.so.1",
                                          });

    const std::vector<std::string> cuda_paths =
            BuildCandidatePaths("CUDA_LIB_PATH", {
                                                     "libcuda.so.1",
                                                     "libcuda.so",
                                                     "/usr/local/nvidia/lib64/libcuda.so.1",
                                                     "/usr/lib/x86_64-linux-gnu/libcuda.so.1",
                                                     "/usr/lib64/libcuda.so.1",
                                                 });

    for (const auto& path : candidate_paths) {
        goldfish::os::DynamicLibrary lib_nvenc(path);
        if (!lib_nvenc.ok()) {
            continue;
        }

        auto create_instance =
                LoadSymbol<NvEncodeAPICreateInstanceFunc>(lib_nvenc, "NvEncodeAPICreateInstance");
        if (!create_instance) {
            continue;
        }

        NV_ENCODE_API_FUNCTION_LIST fn_list = MakeNvencStruct<NV_ENCODE_API_FUNCTION_LIST>();

        NVENCSTATUS status = create_instance(&fn_list);
        if (status != NV_ENC_SUCCESS) {
            LOG(WARNING) << "NvEncodeAPICreateInstance failed on '" << path
                         << "': " << NvencStatusToString(status);
            continue;
        }

        for (const auto& cuda_path : cuda_paths) {
            goldfish::os::DynamicLibrary lib_cuda(cuda_path);
            if (!lib_cuda.ok()) {
                continue;
            }

            auto cu_init = LoadSymbol<CuInitFunc>(lib_cuda, "cuInit");
            auto cu_device_get = LoadSymbol<CuDeviceGetFunc>(lib_cuda, "cuDeviceGet");
            auto cu_ctx_create =
                    LoadSymbol<CuCtxCreateFunc>(lib_cuda, "cuCtxCreate_v2", "cuCtxCreate");
            auto cu_ctx_destroy =
                    LoadSymbol<CuCtxDestroyFunc>(lib_cuda, "cuCtxDestroy_v2", "cuCtxDestroy");

            if (!cu_init || !cu_device_get || !cu_ctx_create || !cu_ctx_destroy) {
                continue;
            }

            if (cu_init(0) != kCudaSuccess) {
                continue;
            }

            int cu_device = 0;
            if (cu_device_get(&cu_device, 0) != kCudaSuccess) {
                continue;
            }

            void* cuda_context = nullptr;
            if (cu_ctx_create(&cuda_context, 0, cu_device) != kCudaSuccess || !cuda_context) {
                continue;
            }

            return std::make_unique<NvencLoader>(std::move(lib_cuda), std::move(lib_nvenc),
                                                 cuda_context, cu_ctx_destroy, fn_list);
        }
    }

    return absl::NotFoundError(absl::StrCat(
            "Could not locate or initialize NVIDIA NVENC and CUDA driver libraries. Checked "
            "paths: [",
            absl::StrJoin(candidate_paths, ", "),
            "]. Remediation: Ensure NVIDIA proprietary drivers (>= 470) are installed "
            "and 'libnvidia-encode.so.1' and 'libcuda.so.1' are accessible."));
}

absl::StatusOr<std::unique_ptr<NvencLoader>> NvencLoader::CreateForTest(
        const NV_ENCODE_API_FUNCTION_LIST& function_list, void* cuda_context) {
    return std::make_unique<NvencLoader>(function_list, cuda_context);
}

NvencLoader::NvencLoader(goldfish::os::DynamicLibrary lib_cuda,
                         goldfish::os::DynamicLibrary lib_nvenc, void* cuda_context,
                         CuCtxDestroyFunc cu_ctx_destroy,
                         const NV_ENCODE_API_FUNCTION_LIST& fn_list)
        : lib_cuda_(std::move(lib_cuda))
        , lib_nvenc_(std::move(lib_nvenc))
        , cuda_context_(cuda_context)
        , cu_ctx_destroy_(cu_ctx_destroy)
        , fn_list_(fn_list) {}

NvencLoader::NvencLoader(const NV_ENCODE_API_FUNCTION_LIST& fn_list, void* cuda_context)
        : cuda_context_(cuda_context), fn_list_(fn_list) {}

NvencLoader::~NvencLoader() {
    Close();
}

NvencLoader::NvencLoader(NvencLoader&& other) noexcept
        : lib_cuda_(std::move(other.lib_cuda_))
        , lib_nvenc_(std::move(other.lib_nvenc_))
        , cuda_context_(std::exchange(other.cuda_context_, nullptr))
        , cu_ctx_destroy_(std::exchange(other.cu_ctx_destroy_, nullptr))
        , fn_list_(other.fn_list_) {}

NvencLoader& NvencLoader::operator=(NvencLoader&& other) noexcept {
    if (this != &other) {
        Close();
        lib_cuda_ = std::move(other.lib_cuda_);
        lib_nvenc_ = std::move(other.lib_nvenc_);
        cuda_context_ = std::exchange(other.cuda_context_, nullptr);
        cu_ctx_destroy_ = std::exchange(other.cu_ctx_destroy_, nullptr);
        fn_list_ = other.fn_list_;
    }
    return *this;
}

void NvencLoader::Close() {
    if (cuda_context_ && cu_ctx_destroy_) {
        cu_ctx_destroy_(cuda_context_);
    }
    cuda_context_ = nullptr;
    cu_ctx_destroy_ = nullptr;
}

}  // namespace goldfish::videobridge
