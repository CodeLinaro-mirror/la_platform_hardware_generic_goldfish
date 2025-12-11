// Copyright 2024 The Android Open Source Project
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

#include "audio_device.h"

#include <cstdlib>
#ifdef __linux__
#include <filesystem>
#endif
#include <initializer_list>
#include <string>
#include <string_view>

#ifdef _WIN32
// clang-format off
// IWYU pragma: begin_keep
#include <windows.h>
#include <mmsystem.h>
#include <DSound.h>
#pragma comment(lib, "Dsound.lib")
#include <objbase.h>
#pragma comment(lib, "Ole32.lib")

#include "goldfish/base/intrusive_ptr.h"
// IWYU pragma: end_keep
// clang-format on
#endif

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include "android/base/file/file.h"
#include "android/goldfish/avd.h"

#ifdef _WIN32
void intrusive_ptr_add_ref(IUnknown* x) {
    x->AddRef();
}
void intrusive_ptr_release(IUnknown* x) {
    x->Release();
}
void intrusive_ptr_ctor(IUnknown* x) {}
#endif

namespace android::goldfish {

absl::Status AudioDevice::initialize(const EmulatorConfig& emulator) {
    return absl::OkStatus();
}

std::vector<std::string> AudioDevice::getQemuParameters(const EmulatorConfig& emulator) const {
    using namespace std::literals;
    const std::string_view kID = "id=mainaudiodev"sv;

    std::string audioBackend = getAudioBackend(emulator.opts());
    std::string_view audioSettings;
    if (audioBackend.empty()) {
        audioBackend = "none"s;
        audioSettings = "out.mixing-engine=off,in.mixing-engine=off"sv;
    } else {
        audioSettings =
                "out.mixing-engine=on,out.fixed-settings=on,"
                "out.frequency=48000,out.format=s16,out.channels=2,"
                "in.mixing-engine=on,in.fixed-settings=on,"
                "in.frequency=48000,in.format=s16,in.channels=1"sv;
    }

    switch (emulator.avd().detectArchitecture()) {
    case Avd::CpuArchitecture::kArm:
    case Avd::CpuArchitecture::kX86:
        return {
            "-audiodev"s,
            absl::StrFormat("%s,%s,%s", audioBackend, kID, audioSettings),
            "-device"s,
            absl::StrCat("virtio-sound-pci,audiodev=mainaudiodev,addr="sv, addr()),
        };

    case Avd::CpuArchitecture::kRiscV:
    case Avd::CpuArchitecture::kUnknown:
        break;
    }

    return {};
}

std::string AudioDevice::getAudioBackend(const AndroidOptions& opts) {
    if (opts.noaudio) {
        return {};
    }

    const char* const audioBackendOpt = opts.audio;
    if (audioBackendOpt && *audioBackendOpt) {
        return audioBackendOpt;
    } else {
        std::string backend = detectHostAudioBackend();
        if (backend.empty()) {
            LOG(WARNING) << "No audio backend detected, there will be no audio.";
        }
        return backend;
    }
}

#if defined(__APPLE__)
// coreaudio.m (coreaudio_audio_init)
std::string AudioDevice::detectHostAudioBackend() {
    using namespace std::literals;
    return "coreaudio"s;
}
#elif defined(__linux__)
// paaudio.c (qpa_audio_init)
std::string AudioDevice::detectHostAudioBackend() {
    using namespace std::literals;
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (!runtime) {
        return {};
    }

    if (!base::file::exists(absl::StrFormat("%s/pulse/pid", runtime))) {
        return {};
    }

    return "pa"s;
}
#elif defined(_WIN32)
using ::goldfish::base::IntrusivePtr;

template <class T>
IntrusivePtr<T> CoCreateInstanceT(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext,
                                  REFIID riid) {
    void* instance;
    HRESULT hr = ::CoCreateInstance(rclsid, pUnkOuter, dwClsContext, riid, &instance);
    if (FAILED(hr)) {
        return {};
    }

    return IntrusivePtr<T>(static_cast<T*>(instance));
}

// dsoundaudio.c (dsound_audio_init)
std::string AudioDevice::detectHostAudioBackend() {
    using namespace std::literals;

    ::CoInitialize(nullptr);

    const auto dsound = CoCreateInstanceT<IDirectSound>(CLSID_DirectSound, nullptr, CLSCTX_ALL,
                                                        IID_IDirectSound);
    if (!dsound || FAILED(dsound->Initialize(nullptr))) {
        return {};
    }

    if (FAILED(dsound->SetCooperativeLevel(GetDesktopWindow(), DSSCL_PRIORITY))) {
        return {};
    }

    const auto dsoundCapture = CoCreateInstanceT<IDirectSoundCapture>(
            CLSID_DirectSoundCapture, nullptr, CLSCTX_ALL, IID_IDirectSoundCapture);
    if (!dsoundCapture || FAILED(dsoundCapture->Initialize(nullptr))) {
        return {};
    }

    return "dsound"s;
}
#else
#error Unexpected platform
#endif

}  // namespace android::goldfish
