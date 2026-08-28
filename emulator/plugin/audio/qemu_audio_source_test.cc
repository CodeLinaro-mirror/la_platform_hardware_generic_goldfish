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

#pragma clang diagnostic ignored "-Wnullability-completeness"

#include "goldfish/audio/qemu_audio_source.h"

#include <gtest/gtest.h>

#include <vector>

#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "test_qemu_audio_stub.h"

namespace goldfish::audio {
namespace {

class FakeAudioTrackSink : public ::webrtc::AudioTrackSinkInterface {
  public:
    void OnData(const void* audio_data, int /*bits_per_sample*/, int /*sample_rate*/,
                size_t number_of_channels, size_t number_of_frames) override {
        const auto* samples = static_cast<const int16_t*>(audio_data);
        size_t total_samples = number_of_frames * number_of_channels;
        received_samples.insert(received_samples.end(), samples, samples + total_samples);
        frames_count++;
    }

    std::vector<int16_t> received_samples;
    size_t frames_count = 0;
};

class QemuAudioSourceTest : public ::testing::Test {
  protected:
    void SetUp() override { test_reset_audio_stubs(); }
    void TearDown() override { test_reset_audio_stubs(); }
};

TEST_F(QemuAudioSourceTest, StartsCaptureOnFirstSinkAndStopsOnLastSink) {
    auto source = ::webrtc::make_ref_counted<QemuAudioSource>(nullptr, 48000, 2);
    FakeAudioTrackSink sink1;
    FakeAudioTrackSink sink2;

    EXPECT_EQ(test_has_active_capture(), 0);

    // 1st sink attaches -> triggers OnStart() and starts QemuAudioCapture
    source->AddSink(&sink1);
    EXPECT_EQ(test_has_active_capture(), 1);

    // 2nd sink attaches -> capture remains active
    source->AddSink(&sink2);
    EXPECT_EQ(test_has_active_capture(), 1);

    // Simulate audio data (10ms of 48kHz stereo = 960 samples)
    std::vector<int16_t> test_frame(960, 4321);
    test_simulate_qemu_audio_output(test_frame.data(), test_frame.size() * sizeof(int16_t));

    EXPECT_EQ(sink1.frames_count, 1);
    EXPECT_EQ(sink2.frames_count, 1);
    ASSERT_EQ(sink1.received_samples.size(), 960);
    EXPECT_EQ(sink1.received_samples[0], 4321);

    // 1st sink detaches -> still active
    source->RemoveSink(&sink1);
    EXPECT_EQ(test_has_active_capture(), 1);

    // 2nd sink detaches -> triggers OnStop() and stops QemuAudioCapture
    source->RemoveSink(&sink2);
    EXPECT_EQ(test_has_active_capture(), 0);
}

}  // namespace
}  // namespace goldfish::audio
