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
#include <gtest/gtest.h>

#include <cstdlib>

#include "api/environment/environment_factory.h"
#include "api/video_codecs/sdp_video_format.h"
#include "goldfish/videobridge/codec_factories.h"

namespace goldfish::videobridge {
namespace {

TEST(CodecFactoriesLinuxTest, CreatesEncoderFactoryWithSupportedFormats) {
    auto factory = CreatePlatformVideoEncoderFactory();
    ASSERT_NE(factory, nullptr);

    auto formats = factory->GetSupportedFormats();
    EXPECT_FALSE(formats.empty());

    // Must at least contain standard software formats (VP8)
    bool has_vp8 = false;
    for (const auto& fmt : formats) {
        if (fmt.name == "VP8") {
            has_vp8 = true;
            break;
        }
    }
    EXPECT_TRUE(has_vp8);
}

TEST(CodecFactoriesLinuxTest, InstantiatesEncoderViaEnvironment) {
    auto factory = CreatePlatformVideoEncoderFactory();
    ASSERT_NE(factory, nullptr);

    auto env = webrtc::CreateEnvironment();
    webrtc::SdpVideoFormat vp8_format("VP8");
    auto encoder = factory->Create(env, vp8_format);
    EXPECT_NE(encoder, nullptr);
}

TEST(CodecFactoriesLinuxTest, EnvironmentOverrideSoftwareMode) {
    ::setenv("ANDROID_EMU_VIDEO_ENCODER", "software", 1);
    auto factory = CreatePlatformVideoEncoderFactory();
    ASSERT_NE(factory, nullptr);

    auto env = webrtc::CreateEnvironment();
    webrtc::SdpVideoFormat vp8_format("VP8");
    auto encoder = factory->Create(env, vp8_format);
    EXPECT_NE(encoder, nullptr);
    ::unsetenv("ANDROID_EMU_VIDEO_ENCODER");
}

TEST(CodecFactoriesLinuxTest, EnvironmentOverrideHardwareModes) {
    ::setenv("ANDROID_EMU_VIDEO_ENCODER", "nvenc", 1);
    auto nvenc_factory = CreatePlatformVideoEncoderFactory();
    EXPECT_NE(nvenc_factory, nullptr);

    ::setenv("ANDROID_EMU_VIDEO_ENCODER", "vaapi", 1);
    auto vaapi_factory = CreatePlatformVideoEncoderFactory();
    EXPECT_NE(vaapi_factory, nullptr);

    ::unsetenv("ANDROID_EMU_VIDEO_ENCODER");
}

TEST(CodecFactoriesLinuxTest, CreatesDecoderFactoryAndDecoder) {
    auto factory = CreatePlatformVideoDecoderFactory();
    ASSERT_NE(factory, nullptr);

    auto formats = factory->GetSupportedFormats();
    EXPECT_FALSE(formats.empty());

    auto env = webrtc::CreateEnvironment();
    webrtc::SdpVideoFormat vp8_format("VP8");
    auto decoder = factory->Create(env, vp8_format);
    EXPECT_NE(decoder, nullptr);
}

}  // namespace
}  // namespace goldfish::videobridge
