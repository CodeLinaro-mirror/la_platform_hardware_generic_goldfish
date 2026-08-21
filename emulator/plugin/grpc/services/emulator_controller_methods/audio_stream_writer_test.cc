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

#include "audio_stream_writer.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <memory>
#include <vector>

#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

#include "emulator/plugin/audio/test_qemu_audio_stub.h"
#include "emulator_controller.grpc.pb.h"
#include "test/grpc_service_test.h"

namespace android::emulation::control {
namespace {

class MockAudioService
        : public EmulatorController::WithCallbackMethod_streamAudio<EmulatorController::Service> {
  public:
    ::grpc::ServerWriteReactor<AudioPacket>* streamAudio(::grpc::CallbackServerContext* /*context*/,
                                                         const AudioFormat* request) override {
        return new AudioStreamWriter(*request);
    }
};

class AudioStreamWriterTest : public GrcpServiceTest {
  protected:
    void SetUp() override {
        test_reset_audio_stubs();
        mService = std::make_unique<MockAudioService>();
        GrcpServiceTest::SetUp();
    }

    void TearDown() override {
        GrcpServiceTest::TearDown();
        test_reset_audio_stubs();
    }

    EmulatorController::Service* getService() override { return mService.get(); }

    std::unique_ptr<MockAudioService> mService;
};

TEST_F(AudioStreamWriterTest, StreamsAudioPacketsToClient) {
    AudioFormat request;
    request.set_samplingrate(44100);
    request.set_channels(AudioFormat::Stereo);

    absl::Notification capture_started;
    test_set_capture_state_callback(
            [](int active, void* user_data) {
                if (active) {
                    static_cast<absl::Notification*>(user_data)->Notify();
                }
            },
            &capture_started);

    auto context = getContextWithTimeout(std::chrono::seconds(30));
    auto reader = mStub->streamAudio(context.get(), request);
    ASSERT_NE(reader, nullptr);
    ASSERT_TRUE(capture_started.WaitForNotificationWithTimeout(absl::Seconds(30)));

    // Simulate QEMU audio output
    int16_t sample_data[4] = {1000, -1000, 2000, -2000};
    test_simulate_qemu_audio_output(sample_data, sizeof(sample_data));

    AudioPacket packet;
    ASSERT_TRUE(reader->Read(&packet));
    EXPECT_EQ(packet.format().samplingrate(), 44100);
    EXPECT_EQ(packet.format().channels(), AudioFormat::Stereo);
    EXPECT_EQ(packet.format().format(), AudioFormat::AUD_FMT_S16);
    EXPECT_GT(packet.timestamp(), 0);
    EXPECT_EQ(packet.audio().size(), sizeof(sample_data));
    EXPECT_EQ(std::memcmp(packet.audio().data(), sample_data, sizeof(sample_data)), 0);

    context->TryCancel();
    AudioPacket ignored;
    while (reader->Read(&ignored)) {
    }
    grpc::Status status = reader->Finish();
    EXPECT_EQ(status.error_code(), grpc::StatusCode::CANCELLED);
}

TEST_F(AudioStreamWriterTest, DefaultFormatWhenUnspecified) {
    AudioFormat request;  // Empty request, samplingRate defaults to 44100Hz, channels to Mono (0)

    absl::Notification capture_started;
    test_set_capture_state_callback(
            [](int active, void* user_data) {
                if (active) {
                    static_cast<absl::Notification*>(user_data)->Notify();
                }
            },
            &capture_started);

    auto context = getContextWithTimeout(std::chrono::seconds(30));
    auto reader = mStub->streamAudio(context.get(), request);
    ASSERT_NE(reader, nullptr);
    ASSERT_TRUE(capture_started.WaitForNotificationWithTimeout(absl::Seconds(30)));

    int16_t sample_data[2] = {500, -500};
    test_simulate_qemu_audio_output(sample_data, sizeof(sample_data));

    AudioPacket packet;
    ASSERT_TRUE(reader->Read(&packet));
    EXPECT_EQ(packet.format().samplingrate(), 44100);
    EXPECT_EQ(packet.format().channels(), AudioFormat::Mono);
    EXPECT_EQ(packet.format().format(), AudioFormat::AUD_FMT_S16);

    context->TryCancel();
    AudioPacket ignored;
    while (reader->Read(&ignored)) {
    }
    reader->Finish();
}

TEST_F(AudioStreamWriterTest, ReturnsErrorIfCaptureStartFails) {
    test_set_fail_add_capture(1);

    AudioFormat request;
    auto context = getContextWithTimeout(std::chrono::seconds(5));
    auto reader = mStub->streamAudio(context.get(), request);
    ASSERT_NE(reader, nullptr);

    AudioPacket packet;
    EXPECT_FALSE(reader->Read(&packet));

    grpc::Status status = reader->Finish();
    EXPECT_EQ(status.error_code(), grpc::StatusCode::INTERNAL);
    EXPECT_NE(status.error_message().find("Failed to start guest audio capture"),
              std::string::npos);
}

TEST_F(AudioStreamWriterTest, ReturnsErrorForInvalidChannels) {
    AudioFormat request;
    request.set_channels(static_cast<AudioFormat::Channels>(99));

    auto context = getContextWithTimeout(std::chrono::seconds(5));
    auto reader = mStub->streamAudio(context.get(), request);
    ASSERT_NE(reader, nullptr);

    AudioPacket packet;
    EXPECT_FALSE(reader->Read(&packet));

    grpc::Status status = reader->Finish();
    EXPECT_EQ(status.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
    EXPECT_NE(status.error_message().find(
                      "Supported options are AudioFormat::Mono (0) or AudioFormat::Stereo (1)"),
              std::string::npos);
}

TEST_F(AudioStreamWriterTest, DropsIncomingPacketsWhenQueueIsFull) {
    AudioFormat request;

    absl::Notification capture_started;
    test_set_capture_state_callback(
            [](int active, void* user_data) {
                if (active) {
                    static_cast<absl::Notification*>(user_data)->Notify();
                }
            },
            &capture_started);

    auto context = getContextWithTimeout(std::chrono::seconds(30));
    auto reader = mStub->streamAudio(context.get(), request);
    ASSERT_NE(reader, nullptr);
    ASSERT_TRUE(capture_started.WaitForNotificationWithTimeout(absl::Seconds(30)));

    int16_t sample_data[2] = {123, -123};
    // Fill the queue to capacity plus an extra 10 packets
    for (size_t i = 0; i < AudioStreamWriter::kMaxQueuedPackets + 10; ++i) {
        test_simulate_qemu_audio_output(sample_data, sizeof(sample_data));
    }

    // Client reads all queued packets
    size_t read_count = 0;
    AudioPacket packet;
    while (read_count < AudioStreamWriter::kMaxQueuedPackets && reader->Read(&packet)) {
        read_count++;
    }
    EXPECT_EQ(read_count, AudioStreamWriter::kMaxQueuedPackets);

    // Cancel and finish
    context->TryCancel();
    while (reader->Read(&packet)) {
    }
    reader->Finish();
}

TEST_F(AudioStreamWriterTest, DropsPacketsContinuouslyWithoutDisconnecting) {
    AudioFormat request;

    absl::Notification capture_started;
    test_set_capture_state_callback(
            [](int active, void* user_data) {
                if (active) {
                    static_cast<absl::Notification*>(user_data)->Notify();
                }
            },
            &capture_started);

    auto context = getContextWithTimeout(std::chrono::seconds(30));
    auto reader = mStub->streamAudio(context.get(), request);
    ASSERT_NE(reader, nullptr);
    ASSERT_TRUE(capture_started.WaitForNotificationWithTimeout(absl::Seconds(30)));

    // Send hundreds of packets without the client reading to cause sustained packet drops
    std::vector<int16_t> sample_data(512, 123);
    for (size_t i = 0; i < 600; ++i) {
        test_simulate_qemu_audio_output(sample_data.data(), sample_data.size() * sizeof(int16_t));
    }

    // Capture remains active
    EXPECT_TRUE(test_has_active_capture());

    // Client can still read queued packets
    AudioPacket packet;
    size_t read_count = 0;
    while (read_count < AudioStreamWriter::kMaxQueuedPackets && reader->Read(&packet)) {
        read_count++;
    }
    EXPECT_EQ(read_count, AudioStreamWriter::kMaxQueuedPackets);

    // Cancel and finish cleanly
    context->TryCancel();
    while (reader->Read(&packet)) {
    }
    grpc::Status status = reader->Finish();
    EXPECT_EQ(status.error_code(), grpc::StatusCode::CANCELLED);
}

}  // namespace
}  // namespace android::emulation::control
