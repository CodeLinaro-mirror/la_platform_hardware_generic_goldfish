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

#include "goldfish_audio_device_module.h"

#include <gtest/gtest.h>

namespace goldfish::videobridge {
namespace {

TEST(GoldfishAudioDeviceModuleTest, CreateReturnsValidReferenceCountedModule) {
    auto module = GoldfishAudioDeviceModule::Create();
    ASSERT_NE(module, nullptr);

    EXPECT_EQ(module->RecordingDevices(), 1);

    bool stereo_available = false;
    EXPECT_EQ(module->StereoRecordingIsAvailable(&stereo_available), 0);
    EXPECT_TRUE(stereo_available);

    bool stereo_enabled = false;
    EXPECT_EQ(module->StereoRecording(&stereo_enabled), 0);
    EXPECT_TRUE(stereo_enabled);
}

}  // namespace
}  // namespace goldfish::videobridge
