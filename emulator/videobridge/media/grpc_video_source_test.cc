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

#include "grpc_video_source.h"

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

#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "emulator_controller.grpc.pb.h"
#include "goldfish/memory/shared_memory.h"

namespace goldfish::videobridge {

using ::android::emulation::control::Image;
using ::android::emulation::control::ImageFormat;
using ::android::emulation::control::ImageTransport;

namespace {

namespace fs = std::filesystem;

class MockEmulatorController final : public EmulatorController::Service {
  public:
    ::grpc::Status streamScreenshot(::grpc::ServerContext* context, const ImageFormat* request,
                                    ::grpc::ServerWriter<Image>* writer) override {
        screenshot_requested_ = true;
        requested_display_id_ = static_cast<int>(request->display());

        Image frame;
        frame.mutable_format()->set_width(2);
        frame.mutable_format()->set_height(2);

        // 12 bytes of BGR888 data (2x2 pixel, 3 bytes/pixel)
        std::string raw_data(12, '\x7F');

        std::unique_ptr<::goldfish::memory::SharedMemory> shm;
        if (request->transport().channel() == ImageTransport::MMAP && !force_payload_delivery_) {
            std::string path_or_uri = request->transport().handle();
            if (path_or_uri.starts_with("file://")) {
                path_or_uri = path_or_uri.substr(7);
            }
            // Open the existing shared memory segment created by client.
            // Size is 12 bytes for 2x2 RGB888.
            shm = std::make_unique<::goldfish::memory::SharedMemory>(path_or_uri, 12);
            const auto status = shm->Open(::goldfish::memory::SharedMemory::AccessMode::kReadWrite);
            if (!status.ok()) {
                return {::grpc::StatusCode::INTERNAL,
                        "Failed to open shared memory: " + status.ToString()};
            }
        } else {
            frame.set_image(raw_data);
        }

        for (int i = 0; i < 3; ++i) {
            if (context->IsCancelled()) {
                break;
            }
            if (shm) {
                // Write directly into shared memory.
                // We use different byte values for each frame to verify it reads fresh data!
                const std::string frame_data(12, static_cast<char>('\x10' + i));
                std::memcpy(shm->Get(), frame_data.data(), frame_data.size());
            } else if (force_payload_delivery_) {
                // If we are forcing payload delivery, we can also vary the payload data per frame
                std::string frame_data(12, static_cast<char>('\x20' + i));
                frame.set_image(std::move(frame_data));
            }
            writer->Write(frame);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return ::grpc::Status::OK;
    }

    bool ScreenshotRequested() const { return screenshot_requested_; }
    int RequestedDisplayId() const { return requested_display_id_; }
    void SetForcePayloadDelivery(bool force) { force_payload_delivery_ = force; }

  private:
    std::atomic<bool> screenshot_requested_{false};
    std::atomic<int> requested_display_id_{-1};
    std::atomic<bool> force_payload_delivery_{false};
};

class TestVideoSink : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
  public:
    void OnFrame(const webrtc::VideoFrame& frame) override {
        const absl::MutexLock lock(&mutex_);
        last_frame_ = frame;
        frame_count_++;
    }

    int FrameCount() const {
        const absl::MutexLock lock(&mutex_);
        return frame_count_;
    }

    webrtc::VideoFrame LastFrame() const {
        const absl::MutexLock lock(&mutex_);
        if (!last_frame_.has_value()) {
            return webrtc::VideoFrame::Builder().build();
        }
        return *last_frame_;
    }

  private:
    mutable absl::Mutex mutex_;
    int frame_count_ ABSL_GUARDED_BY(mutex_) = 0;
    absl::optional<webrtc::VideoFrame> last_frame_ ABSL_GUARDED_BY(mutex_);
};

class GrpcVideoSourceTest : public ::testing::Test {
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
                    "video_source_test_",
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

TEST_F(GrpcVideoSourceTest, GrpcVideoSourceStreamsFramesFromClient) {
    StartServer();
    const size_t colon = server_address_.find(':');
    const std::string port = server_address_.substr(colon + 1);

    const TmpDiscoveryFile tmp_file(absl::StrCat("grpc.port = ", port));
    auto client = std::make_shared<EmulatorClient>(tmp_file.Path().string());
    ASSERT_TRUE(client->Connect(absl::Seconds(2)).ok());

    GrpcVideoSourceOptions options;
    options.display_id = 1;
    options.width = 2;
    options.height = 2;
    options.transport = GrpcVideoSourceOptions::Transport::kGrpcBytes;

    auto source = ::webrtc::make_ref_counted<GrpcVideoSource>(client, options);
    TestVideoSink sink;
    webrtc::VideoSourceInterface<webrtc::VideoFrame>* source_interface = source.get();
    source_interface->AddOrUpdateSink(&sink, webrtc::VideoSinkWants());

    source->Start();

    // Wait for frames to arrive.
    int retries = 50;
    while (sink.FrameCount() < 3 && retries-- > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    source->Stop();
    source_interface->RemoveSink(&sink);

    EXPECT_GE(sink.FrameCount(), 3);
    EXPECT_TRUE(service_.ScreenshotRequested());
    EXPECT_EQ(service_.RequestedDisplayId(), 1);

    // Verify properties of the converted frame
    const webrtc::VideoFrame frame = sink.LastFrame();
    EXPECT_EQ(frame.width(), 2);
    EXPECT_EQ(frame.height(), 2);
    ASSERT_NE(frame.video_frame_buffer(), nullptr);
    const ::webrtc::scoped_refptr<webrtc::I420BufferInterface> i420 =
            frame.video_frame_buffer()->ToI420();
    ASSERT_NE(i420, nullptr);
    EXPECT_EQ(i420->width(), 2);
    EXPECT_EQ(i420->height(), 2);

    client->Disconnect();
}

TEST_F(GrpcVideoSourceTest, GrpcVideoSourceStreamsFramesViaSharedMemory) {
    StartServer();
    const size_t colon = server_address_.find(':');
    const std::string port = server_address_.substr(colon + 1);

    const TmpDiscoveryFile tmp_file(absl::StrCat("grpc.port = ", port));
    auto client = std::make_shared<EmulatorClient>(tmp_file.Path().string());
    ASSERT_TRUE(client->Connect(absl::Seconds(2)).ok());

    // Setup Shared Memory option path
    const std::string shm_filename =
            absl::StrCat("grpc_video_source_test_shm_",
                         absl::Hex(absl::Uniform<uint64_t>(absl::BitGen()), absl::kSpacePad16));
    const fs::path shm_path = fs::temp_directory_path() / shm_filename;

    GrpcVideoSourceOptions options;
    options.display_id = 1;
    options.width = 2;
    options.height = 2;
    options.transport = GrpcVideoSourceOptions::Transport::kSharedMemory;
    options.shared_memory_path = shm_path;

    auto source = ::webrtc::make_ref_counted<GrpcVideoSource>(client, options);
    TestVideoSink sink;
    webrtc::VideoSourceInterface<webrtc::VideoFrame>* source_interface = source.get();
    source_interface->AddOrUpdateSink(&sink, webrtc::VideoSinkWants());

    source->Start();

    // Wait for frames to arrive.
    int retries = 50;
    while (sink.FrameCount() < 3 && retries-- > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    source->Stop();
    source_interface->RemoveSink(&sink);

    EXPECT_GE(sink.FrameCount(), 3);
    EXPECT_TRUE(service_.ScreenshotRequested());
    EXPECT_EQ(service_.RequestedDisplayId(), 1);

    // Verify properties of the last converted frame read from shared memory.
    const webrtc::VideoFrame frame = sink.LastFrame();
    EXPECT_EQ(frame.width(), 2);
    EXPECT_EQ(frame.height(), 2);
    ASSERT_NE(frame.video_frame_buffer(), nullptr);
    const ::webrtc::scoped_refptr<webrtc::I420BufferInterface> i420 =
            frame.video_frame_buffer()->ToI420();
    ASSERT_NE(i420, nullptr);
    EXPECT_EQ(i420->width(), 2);
    EXPECT_EQ(i420->height(), 2);

    // Since the mock wrote a constant value of 0x12 (18) to all channels (RGB),
    // the converted Y channel in limited-range YUV is: 16 + 0.859 * 18 = ~31.
    const uint8_t* y_data = i420->DataY();
    ASSERT_NE(y_data, nullptr);
    EXPECT_NEAR(y_data[0], 31, 2);

    client->Disconnect();
}

TEST_F(GrpcVideoSourceTest, GrpcVideoSourceFallsBackToBytesIfSharedMemoryNotMapped) {
    StartServer();
    const size_t colon = server_address_.find(':');
    const std::string port = server_address_.substr(colon + 1);

    const TmpDiscoveryFile tmp_file(absl::StrCat("grpc.port = ", port));
    auto client = std::make_shared<EmulatorClient>(tmp_file.Path().string());
    ASSERT_TRUE(client->Connect(absl::Seconds(2)).ok());

    // Force the mock server to ignore MMAP request and send the image in the proto payload
    service_.SetForcePayloadDelivery(true);

    // We request kSharedMemory, but we provide an invalid/uncreatable path.
    // This causes shared_memory_->Create() to fail, so shared_memory_->IsMapped() is false,
    // forcing the client to fall back to reading from the proto payload.
    GrpcVideoSourceOptions options;
    options.display_id = 1;
    options.width = 2;
    options.height = 2;
    options.transport = GrpcVideoSourceOptions::Transport::kSharedMemory;
    // An empty path or a path in a non-existent directory will cause creation to fail.
    options.shared_memory_path = "/non_existent_directory/invalid_shm_path";

    auto source = ::webrtc::make_ref_counted<GrpcVideoSource>(client, options);
    TestVideoSink sink;
    webrtc::VideoSourceInterface<webrtc::VideoFrame>* source_interface = source.get();
    source_interface->AddOrUpdateSink(&sink, webrtc::VideoSinkWants());

    source->Start();

    // Wait for frames to arrive.
    int retries = 50;
    while (sink.FrameCount() < 3 && retries-- > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    source->Stop();
    source_interface->RemoveSink(&sink);

    EXPECT_GE(sink.FrameCount(), 3);
    EXPECT_TRUE(service_.ScreenshotRequested());
    EXPECT_EQ(service_.RequestedDisplayId(), 1);

    // Verify properties of the last converted frame read from protobuf payload.
    const webrtc::VideoFrame frame = sink.LastFrame();
    EXPECT_EQ(frame.width(), 2);
    EXPECT_EQ(frame.height(), 2);
    ASSERT_NE(frame.video_frame_buffer(), nullptr);
    const ::webrtc::scoped_refptr<webrtc::I420BufferInterface> i420 =
            frame.video_frame_buffer()->ToI420();
    ASSERT_NE(i420, nullptr);

    // Since the mock wrote a constant value of 0x22 (34) to all channels (RGB) for the last frame,
    // the converted Y channel in limited-range YUV is: 16 + 0.859 * 34 = ~45.
    const uint8_t* y_data = i420->DataY();
    ASSERT_NE(y_data, nullptr);
    EXPECT_NEAR(y_data[0], 45, 2);

    client->Disconnect();
}

}  // namespace
}  // namespace goldfish::videobridge
