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

#include "grpc_audio_source.h"

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"

#include "api/media_stream_interface.h"
#include "api/scoped_refptr.h"
#include "emulator_controller.grpc.pb.h"

namespace goldfish::videobridge {
namespace {

namespace fs = std::filesystem;

class MockEmulatorController final : public EmulatorController::Service {
  public:
    ::grpc::Status streamAudio(::grpc::ServerContext* context, const AudioFormat* /*request*/,
                               ::grpc::ServerWriter<AudioPacket>* writer) override {
        audio_requested_ = true;

        if (return_unimplemented_) {
            return {::grpc::StatusCode::UNIMPLEMENTED, "Audio service unimplemented"};
        }

        // Write three packets of 1000 bytes each.
        // Total bytes = 3000 bytes.
        // kBytesPerFrame = 1764 bytes.
        // 3000 bytes should yield exactly 1 frame delivered, with 1236 bytes buffered.
        AudioPacket packet;
        packet.set_audio(std::string(1000, '\x0A'));

        for (int i = 0; i < 3; ++i) {
            if (context->IsCancelled()) {
                break;
            }
            writer->Write(packet);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return ::grpc::Status::OK;
    }

    bool AudioRequested() const { return audio_requested_; }
    void SetReturnUnimplemented(bool val) { return_unimplemented_ = val; }

  private:
    std::atomic<bool> audio_requested_{false};
    std::atomic<bool> return_unimplemented_{false};
};

class TestAudioSink : public webrtc::AudioTrackSinkInterface {
  public:
    void OnData(const void* /*audio_data*/, int /*bits_per_sample*/, int /*sample_rate*/,
                size_t number_of_channels, size_t number_of_frames) override {
        const absl::MutexLock lock(&mutex_);
        frame_count_++;
        total_frames_received_ += number_of_frames;
        total_samples_received_ += number_of_frames * number_of_channels;
    }

    int FrameCount() const {
        const absl::MutexLock lock(&mutex_);
        return frame_count_;
    }

    size_t TotalFramesReceived() const {
        const absl::MutexLock lock(&mutex_);
        return total_frames_received_;
    }

    size_t TotalSamplesReceived() const {
        const absl::MutexLock lock(&mutex_);
        return total_samples_received_;
    }

  private:
    mutable absl::Mutex mutex_;
    int frame_count_ ABSL_GUARDED_BY(mutex_) = 0;
    size_t total_frames_received_ ABSL_GUARDED_BY(mutex_) = 0;
    size_t total_samples_received_ ABSL_GUARDED_BY(mutex_) = 0;
};

class GrpcAudioSourceTest : public ::testing::Test {
  protected:
    void TearDown() override {
        if (server_) {
            server_->Shutdown();
        }
    }

    void StartServer() {
        server_address_ = "localhost:0";
        ::grpc::ServerBuilder builder;
        int selected_port = 0;
        builder.AddListeningPort(server_address_, ::grpc::InsecureServerCredentials(),
                                 &selected_port);
        builder.RegisterService(&service_);
        server_ = builder.BuildAndStart();
        ASSERT_NE(server_, nullptr);
        server_address_ = "localhost:" + std::to_string(selected_port);
    }

    class TmpDiscoveryFile {
      public:
        explicit TmpDiscoveryFile(const std::string& content) {
            const std::string file_name = absl::StrCat(
                    "audio_source_test_",
                    absl::Hex(absl::Uniform<uint64_t>(absl::BitGen()), absl::kSpacePad16));
            path_ = fs::temp_directory_path() / file_name;
            std::ofstream out(path_);
            out << content;
        }

        ~TmpDiscoveryFile() {
            std::error_code ec;
            fs::remove(path_, ec);
        }

        const fs::path& Path() const { return path_; }

      private:
        fs::path path_;
    };

    MockEmulatorController service_;
    std::unique_ptr<::grpc::Server> server_;
    std::string server_address_;
};

TEST_F(GrpcAudioSourceTest, GrpcAudioSourceStreamsAndBuffersPackets) {
    StartServer();
    const size_t colon = server_address_.find(':');
    const std::string port = server_address_.substr(colon + 1);

    const TmpDiscoveryFile tmp_file(absl::StrCat("grpc.port = ", port));
    auto client = std::make_shared<EmulatorClient>(tmp_file.Path().string());
    ASSERT_TRUE(client->Connect(absl::Seconds(2)).ok());

    auto source = ::webrtc::make_ref_counted<GrpcAudioSource>(client);
    TestAudioSink sink;
    source->AddSink(&sink);

    source->Start();

    // Wait for frames. We expect exactly 1 frame because 3000 bytes / 1764 bytes = 1.7 frames.
    int retries = 50;
    while (sink.FrameCount() < 1 && retries-- > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    source->Stop();
    source->RemoveSink(&sink);

    EXPECT_EQ(sink.FrameCount(), 1);
    EXPECT_EQ(sink.TotalFramesReceived(), 441);   // 441 samples per 10ms frame at 44100Hz
    EXPECT_EQ(sink.TotalSamplesReceived(), 882);  // 2 channels
    EXPECT_TRUE(service_.AudioRequested());

    client->Disconnect();
}

TEST_F(GrpcAudioSourceTest, GracefullyHandlesUnimplementedAudioStream) {
    service_.SetReturnUnimplemented(true);
    StartServer();
    const size_t colon = server_address_.find(':');
    const std::string port = server_address_.substr(colon + 1);

    const TmpDiscoveryFile tmp_file(absl::StrCat("grpc.port = ", port));
    auto client = std::make_shared<EmulatorClient>(tmp_file.Path().string());
    ASSERT_TRUE(client->Connect(absl::Seconds(2)).ok());

    auto source = ::webrtc::make_ref_counted<GrpcAudioSource>(client);
    TestAudioSink sink;
    source->AddSink(&sink);

    source->Start();

    // The stream should fail immediately with UNIMPLEMENTED and exit.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    source->Stop();
    source->RemoveSink(&sink);

    EXPECT_EQ(sink.FrameCount(), 0);
    EXPECT_TRUE(service_.AudioRequested());

    client->Disconnect();
}

}  // namespace
}  // namespace goldfish::videobridge
