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

// Disable compiler warnings for external third-party headers. We wrap these in localized
// pragma blocks rather than using target 'copts' so that thread-safety analysis remains
// active on our own local source files.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "api/scoped_refptr.h"
#include "modules/audio_device/include/fake_audio_device.h"
#include "rtc_base/ref_counted_object.h"
#pragma clang diagnostic pop

namespace goldfish::videobridge {

/**
 * @class GoldfishAudioDeviceModule
 * @brief Fake audio device module subclassing WebRTC's FakeAudioDeviceModule.
 *
 * This class convinces the WebRTC audio engine that a stereo recording device is present.
 * It is required to initialize WebRTC audio pipelines on systems/emulators that do not
 * expose a real hardware microphone interface.
 */
class GoldfishAudioDeviceModule : public ::webrtc::FakeAudioDeviceModule {
  public:
    /**
     * @brief Factory method creating a properly reference-counted GoldfishAudioDeviceModule.
     *
     * WebRTC AudioDeviceModule instances must be wrapped in a reference counter (such as
     * webrtc::RefCountedObject) to ensure safe reference counting and deallocation via
     * scoped_refptr. Direct construction via new or stack allocation is prevented by
     * protected constructors to avoid memory leaks and lifetime errors.
     *
     * @return A scoped_refptr owning a reference-counted GoldfishAudioDeviceModule instance.
     */
    static ::webrtc::scoped_refptr<GoldfishAudioDeviceModule> Create() {
        return ::webrtc::scoped_refptr<GoldfishAudioDeviceModule>(
                new ::webrtc::RefCountedObject<GoldfishAudioDeviceModule>());
    }

    /**
     * @brief Reports the number of recording devices.
     * @return Always returns 1 to indicate a single recording channel is available.
     */
    int16_t RecordingDevices() override { return 1; }

    /**
     * @brief Declares if stereo recording is available.
     *
     * @param available Output pointer populated with true.
     * @return Always returns 0 (success status).
     */
    int32_t StereoRecordingIsAvailable(bool* available) const override {
        *available = true;
        return 0;
    }

    /**
     * @brief Declares if stereo recording is enabled.
     *
     * @param enabled Output pointer populated with true.
     * @return Always returns 0 (success status).
     */
    int32_t StereoRecording(bool* enabled) const override {
        *enabled = true;
        return 0;
    }

  protected:
    GoldfishAudioDeviceModule() = default;
    ~GoldfishAudioDeviceModule() override = default;

  private:
    friend class ::webrtc::RefCountedObject<GoldfishAudioDeviceModule>;
};

}  // namespace goldfish::videobridge
