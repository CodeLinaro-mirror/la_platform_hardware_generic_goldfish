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

#include "goldfish/videobridge/in_process_audio_source.h"

#include <gtest/gtest.h>

#include <vector>

#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"

namespace goldfish::videobridge {
namespace {

class DummyAudioSink : public ::webrtc::AudioTrackSinkInterface {
  public:
    void OnData(const void* audio_data, int bits_per_sample, int sample_rate,
                size_t number_of_channels, size_t number_of_frames) override {
        frame_count++;
        last_sample_rate = sample_rate;
        last_channels = number_of_channels;
        last_frames = number_of_frames;
        last_bits = bits_per_sample;
        const auto* samples = reinterpret_cast<const int16_t*>(audio_data);
        size_t total_samples = number_of_frames * number_of_channels;
        received_samples.insert(received_samples.end(), samples, samples + total_samples);
    }

    int frame_count = 0;
    int last_sample_rate = 0;
    size_t last_channels = 0;
    size_t last_frames = 0;
    int last_bits = 0;
    std::vector<int16_t> received_samples;
};

TEST(InProcessAudioSourceTest, SlicesStreamInto10msFrames) {
    auto source = ::webrtc::make_ref_counted<InProcessAudioSource>(44100, 2);
    DummyAudioSink sink;

    source->AddSink(&sink);
    source->Start();

    // 44100Hz stereo = 441 samples/channel/10ms = 882 total samples per 10ms
    // Push 20ms worth of audio samples (1764 samples)
    std::vector<int16_t> pcm_data(1764, 42);
    source->OnAudioData(pcm_data.data(), pcm_data.size());

    EXPECT_EQ(sink.frame_count, 2);
    EXPECT_EQ(sink.last_sample_rate, 44100);
    EXPECT_EQ(sink.last_channels, 2U);
    EXPECT_EQ(sink.last_frames, 441U);
    EXPECT_EQ(sink.last_bits, 16);

    source->Stop();
    source->RemoveSink(&sink);
}

TEST(InProcessAudioSourceTest, RingBufferWrapAroundMaintainsSampleIntegrity) {
    auto source = ::webrtc::make_ref_counted<InProcessAudioSource>(44100, 2);
    DummyAudioSink sink;

    source->AddSink(&sink);
    source->Start();

    // Push 100 unaligned chunks of 500 samples (50,000 samples total)
    // 50,000 / 882 = 56 complete 10ms frames (49,392 samples delivered)
    std::vector<int16_t> expected_all_samples;
    expected_all_samples.reserve(50000);
    int16_t sample_counter = 0;

    for (int i = 0; i < 100; ++i) {
        std::vector<int16_t> chunk(500);
        for (auto& s : chunk) {
            s = sample_counter++;
            expected_all_samples.push_back(s);
        }
        source->OnAudioData(chunk.data(), chunk.size());
    }

    EXPECT_EQ(sink.frame_count, 56);
    EXPECT_EQ(sink.received_samples.size(), 56U * 882U);

    // Verify sample values match sequential expectation without offset corruption
    for (size_t i = 0; i < sink.received_samples.size(); ++i) {
        ASSERT_EQ(sink.received_samples[i], expected_all_samples[i])
                << "Mismatch at sample index " << i;
    }

    source->Stop();
    source->RemoveSink(&sink);
}

TEST(InProcessAudioSourceTest, StartStopLifecycleDropsSamplesWhenStoppedAndFlushesBuffer) {
    auto source = ::webrtc::make_ref_counted<InProcessAudioSource>(44100, 2);
    DummyAudioSink sink;

    source->AddSink(&sink);

    // After adding a sink, source is automatically running.
    // Push 1 full frame (882 samples).
    std::vector<int16_t> pcm_data(882, 100);
    source->OnAudioData(pcm_data.data(), pcm_data.size());
    EXPECT_EQ(sink.frame_count, 1);

    // Push partial frame (441 samples)
    std::vector<int16_t> partial_frame(441, 200);
    source->OnAudioData(partial_frame.data(), partial_frame.size());
    EXPECT_EQ(sink.frame_count, 1);

    // Stop source (flushes partial buffer)
    source->Stop();

    // Pushing samples while stopped should be dropped
    source->OnAudioData(pcm_data.data(), pcm_data.size());
    EXPECT_EQ(sink.frame_count, 1);

    // Re-start source and push fresh frame (882 samples)
    std::vector<int16_t> fresh_frame(882, 300);
    source->Start();
    source->OnAudioData(fresh_frame.data(), fresh_frame.size());
    EXPECT_EQ(sink.frame_count, 2);

    // The second frame should only contain fresh_frame (value 300), not partial_frame
    for (size_t i = 882; i < sink.received_samples.size(); ++i) {
        EXPECT_EQ(sink.received_samples[i], 300);
    }

    source->Stop();
    source->RemoveSink(&sink);
}

TEST(InProcessAudioSourceTest, AutomaticSinkRegistrationLifecycle) {
    auto source = ::webrtc::make_ref_counted<InProcessAudioSource>(44100, 2);
    DummyAudioSink sink;

    // Pushing before any sink is added should be dropped
    std::vector<int16_t> pcm_data(882, 42);
    source->OnAudioData(pcm_data.data(), pcm_data.size());
    EXPECT_EQ(sink.frame_count, 0);

    // Adding sink auto-starts capture
    source->AddSink(&sink);
    source->OnAudioData(pcm_data.data(), pcm_data.size());
    EXPECT_EQ(sink.frame_count, 1);

    // Removing sink auto-stops capture
    source->RemoveSink(&sink);
    source->OnAudioData(pcm_data.data(), pcm_data.size());
    EXPECT_EQ(sink.frame_count, 1);
}

TEST(InProcessAudioSourceTest, MultipleSinksAndDynamicAddRemove) {
    auto source = ::webrtc::make_ref_counted<InProcessAudioSource>(44100, 2);
    DummyAudioSink sink1;
    DummyAudioSink sink2;

    source->AddSink(&sink1);
    source->AddSink(&sink2);
    source->Start();

    std::vector<int16_t> pcm_data(882, 55);
    source->OnAudioData(pcm_data.data(), pcm_data.size());

    EXPECT_EQ(sink1.frame_count, 1);
    EXPECT_EQ(sink2.frame_count, 1);

    // Remove sink1; subsequent audio should only go to sink2
    source->RemoveSink(&sink1);
    source->OnAudioData(pcm_data.data(), pcm_data.size());

    EXPECT_EQ(sink1.frame_count, 1);
    EXPECT_EQ(sink2.frame_count, 2);

    source->Stop();
    source->RemoveSink(&sink2);
}

TEST(InProcessAudioSourceTest, Supports48kHzStereoAnd16kHzMono) {
    // 48000 Hz Stereo: 480 samples/channel/10ms = 960 total samples/10ms
    {
        auto source_48k = ::webrtc::make_ref_counted<InProcessAudioSource>(48000, 2);
        DummyAudioSink sink_48k;

        source_48k->AddSink(&sink_48k);
        source_48k->Start();

        std::vector<int16_t> pcm_48k(1920, 1);  // 20ms
        source_48k->OnAudioData(pcm_48k.data(), pcm_48k.size());

        EXPECT_EQ(sink_48k.frame_count, 2);
        EXPECT_EQ(sink_48k.last_sample_rate, 48000);
        EXPECT_EQ(sink_48k.last_channels, 2U);
        EXPECT_EQ(sink_48k.last_frames, 480U);

        source_48k->Stop();
        source_48k->RemoveSink(&sink_48k);
    }

    // 16000 Hz Mono: 160 samples/channel/10ms = 160 total samples/10ms
    {
        auto source_16k = ::webrtc::make_ref_counted<InProcessAudioSource>(16000, 1);
        DummyAudioSink sink_16k;

        source_16k->AddSink(&sink_16k);
        source_16k->Start();

        std::vector<int16_t> pcm_16k(320, 2);  // 20ms
        source_16k->OnAudioData(pcm_16k.data(), pcm_16k.size());

        EXPECT_EQ(sink_16k.frame_count, 2);
        EXPECT_EQ(sink_16k.last_sample_rate, 16000);
        EXPECT_EQ(sink_16k.last_channels, 1U);
        EXPECT_EQ(sink_16k.last_frames, 160U);

        source_16k->Stop();
        source_16k->RemoveSink(&sink_16k);
    }
}

TEST(InProcessAudioSourceTest, IgnoresNullOrEmptyAudioData) {
    auto source = ::webrtc::make_ref_counted<InProcessAudioSource>(44100, 2);
    DummyAudioSink sink;

    source->AddSink(&sink);
    source->Start();

    source->OnAudioData(nullptr, 1000);
    std::vector<int16_t> empty;
    source->OnAudioData(empty.data(), 0);

    EXPECT_EQ(sink.frame_count, 0);

    source->Stop();
    source->RemoveSink(&sink);
}

TEST(InProcessAudioSourceTest, OptionsDisableVoiceDSP) {
    auto source = ::webrtc::make_ref_counted<InProcessAudioSource>(44100, 2);
    auto opts = source->options();

    EXPECT_FALSE(opts.echo_cancellation.value_or(true));
    EXPECT_FALSE(opts.auto_gain_control.value_or(true));
    EXPECT_FALSE(opts.noise_suppression.value_or(true));
    EXPECT_FALSE(opts.highpass_filter.value_or(true));
}

}  // namespace
}  // namespace goldfish::videobridge
