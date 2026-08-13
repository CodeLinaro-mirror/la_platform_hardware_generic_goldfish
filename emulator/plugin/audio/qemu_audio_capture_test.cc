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

#include "goldfish/audio/qemu_audio_capture.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "absl/status/status.h"

#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "goldfish/videobridge/in_process_audio_source.h"
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

class QemuAudioCaptureTest : public ::testing::Test {
  protected:
    void SetUp() override { test_reset_audio_stubs(); }

    void TearDown() override { test_reset_audio_stubs(); }
};

TEST_F(QemuAudioCaptureTest, StartAndStopLifecycle) {
    QemuAudioCapture capture([](const int16_t*, size_t) {});

    EXPECT_EQ(capture.sample_rate_hz(), 48000U);
    EXPECT_EQ(capture.channels(), 2U);
    EXPECT_FALSE(capture.IsRunning());
    EXPECT_TRUE(capture.Start().ok());
    EXPECT_TRUE(capture.IsRunning());

    capture.Stop();
    EXPECT_FALSE(capture.IsRunning());
}

TEST_F(QemuAudioCaptureTest, PushesSamplesToCallback) {
    std::vector<int16_t> captured_samples;
    QemuAudioCapture capture([&captured_samples](const int16_t* pcm_data, size_t num_samples) {
        captured_samples.insert(captured_samples.end(), pcm_data, pcm_data + num_samples);
    });

    ASSERT_TRUE(capture.Start().ok());

    std::vector<int16_t> test_pcm = {100, 200, -100, -200, 300, 400};
    test_simulate_qemu_audio_output(test_pcm.data(), test_pcm.size() * sizeof(int16_t));

    EXPECT_EQ(captured_samples, test_pcm);
    capture.Stop();
}

TEST_F(QemuAudioCaptureTest, PushesSamplesToInProcessAudioSource) {
    auto source = ::webrtc::make_ref_counted<videobridge::InProcessAudioSource>(48000, 2);
    FakeAudioTrackSink sink;
    source->AddSink(&sink);

    QemuAudioCapture capture(
            [source](const int16_t* pcm_data, size_t num_samples) {
                source->OnAudioData(pcm_data, num_samples);
            },
            48000, 2);
    source->Start();
    ASSERT_TRUE(capture.Start().ok());

    // 10ms of 48kHz stereo audio = 480 frames * 2 channels = 960 samples
    std::vector<int16_t> test_frame(960, 1234);
    test_simulate_qemu_audio_output(test_frame.data(), test_frame.size() * sizeof(int16_t));

    EXPECT_EQ(sink.frames_count, 1);
    ASSERT_EQ(sink.received_samples.size(), 960);
    EXPECT_EQ(sink.received_samples[0], 1234);

    capture.Stop();
    source->Stop();
    source->RemoveSink(&sink);
}

TEST_F(QemuAudioCaptureTest, HandlesBackendFailureGracefully) {
    test_set_fail_add_capture(1);

    QemuAudioCapture capture([](const int16_t*, size_t) {});

    auto status = capture.Start();
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.code(), absl::StatusCode::kInternal);
    EXPECT_FALSE(capture.IsRunning());
}

TEST_F(QemuAudioCaptureTest, HandlesNotifyWithoutCrashing) {
    QemuAudioCapture capture([](const int16_t*, size_t) {});

    ASSERT_TRUE(capture.Start().ok());
    test_simulate_qemu_notify(0);  // AUD_CNOTIFY_ENABLE
    test_simulate_qemu_notify(1);  // AUD_CNOTIFY_DISABLE
    EXPECT_TRUE(capture.IsRunning());
    capture.Stop();
}

TEST_F(QemuAudioCaptureTest, IdempotentStartAndStop) {
    QemuAudioCapture capture([](const int16_t*, size_t) {});

    EXPECT_TRUE(capture.Start().ok());
    EXPECT_TRUE(capture.Start().ok());
    EXPECT_TRUE(capture.IsRunning());

    capture.Stop();
    capture.Stop();
    EXPECT_FALSE(capture.IsRunning());
}

TEST_F(QemuAudioCaptureTest, ConcurrentStartCallsOnlyActivateOnce) {
    QemuAudioCapture capture([](const int16_t*, size_t) {});

    constexpr int kNumThreads = 10;
    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);

    std::atomic<bool> start_gate{false};
    std::atomic<int> success_count{0};

    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back([&capture, &start_gate, &success_count]() {
            while (!start_gate.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            if (capture.Start().ok()) {
                success_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    start_gate.store(true, std::memory_order_release);
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), kNumThreads);
    EXPECT_TRUE(capture.IsRunning());
    EXPECT_TRUE(test_has_active_capture());

    capture.Stop();
    EXPECT_FALSE(capture.IsRunning());
    EXPECT_FALSE(test_has_active_capture());
}

TEST_F(QemuAudioCaptureTest, ConcurrentStopCallsFromMultipleThreads) {
    QemuAudioCapture capture([](const int16_t*, size_t) {});

    ASSERT_TRUE(capture.Start().ok());
    EXPECT_TRUE(capture.IsRunning());
    EXPECT_TRUE(test_has_active_capture());

    constexpr int kNumThreads = 10;
    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);

    std::atomic<bool> start_gate{false};
    for (int i = 0; i < kNumThreads; ++i) {
        threads.emplace_back([&capture, &start_gate]() {
            while (!start_gate.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            capture.Stop();
        });
    }

    start_gate.store(true, std::memory_order_release);
    for (auto& t : threads) {
        t.join();
    }

    EXPECT_FALSE(capture.IsRunning());
    EXPECT_FALSE(test_has_active_capture());
}

TEST_F(QemuAudioCaptureTest, ConcurrentStopWhileReceivingAudio) {
    std::atomic<size_t> samples_received{0};
    QemuAudioCapture capture([&samples_received](const int16_t*, size_t num_samples) {
        samples_received.fetch_add(num_samples, std::memory_order_relaxed);
    });

    ASSERT_TRUE(capture.Start().ok());

    std::atomic<bool> audio_running{true};
    std::thread audio_thread([&audio_running]() {
        std::vector<int16_t> pcm_chunk(480, 42);
        while (audio_running.load(std::memory_order_relaxed)) {
            test_simulate_qemu_audio_output(pcm_chunk.data(), pcm_chunk.size() * sizeof(int16_t));
            std::this_thread::yield();
        }
    });

    constexpr int kNumThreads = 5;
    std::vector<std::thread> stop_threads;
    stop_threads.reserve(kNumThreads);

    for (int i = 0; i < kNumThreads; ++i) {
        stop_threads.emplace_back([&capture]() { capture.Stop(); });
    }

    for (auto& t : stop_threads) {
        t.join();
    }

    audio_running.store(false, std::memory_order_relaxed);
    audio_thread.join();

    EXPECT_FALSE(capture.IsRunning());
    EXPECT_FALSE(test_has_active_capture());
}

}  // namespace
}  // namespace goldfish::audio
