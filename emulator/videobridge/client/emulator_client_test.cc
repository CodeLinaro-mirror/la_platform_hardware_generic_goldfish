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

#include "goldfish/videobridge/emulator_client.h"

#include <grpcpp/grpcpp.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"

#include "emulator_controller.grpc.pb.h"

namespace goldfish::videobridge {
namespace {

namespace fs = std::filesystem;

class MockEmulatorController final : public EmulatorController::Service {
  public:
    ::grpc::Status streamScreenshot(::grpc::ServerContext* context, const ImageFormat* request,
                                    ::grpc::ServerWriter<Image>* writer) override {
        mScreenshotRequested = true;
        mRequestedDisplayId = request->display();
        Image frame;
        frame.set_width(1280);
        frame.set_height(720);
        writer->Write(frame);
        return ::grpc::Status::OK;
    }

    ::grpc::Status streamAudio(::grpc::ServerContext* context, const AudioFormat* request,
                               ::grpc::ServerWriter<AudioPacket>* writer) override {
        mAudioRequested = true;
        AudioPacket packet;
        packet.set_audio("mock-pcm-data");
        writer->Write(packet);
        return ::grpc::Status::OK;
    }

    ::grpc::Status streamInputEvent(::grpc::ServerContext* context,
                                    ::grpc::ServerReader<InputEvent>* reader,
                                    ::google::protobuf::Empty* response) override {
        mEventStreamOpened = true;
        InputEvent event;
        while (reader->Read(&event)) {
            mReceivedEvents.push_back(event);
        }
        return ::grpc::Status::OK;
    }

    bool mScreenshotRequested = false;
    int mRequestedDisplayId = -1;
    bool mAudioRequested = false;
    bool mEventStreamOpened = false;
    std::vector<InputEvent> mReceivedEvents;
};

class EmulatorClientTest : public ::testing::Test {
  protected:
    void TearDown() override {
        if (server) {
            server->Shutdown();
        }
    }

    void StartServer() {
        server_address = "localhost:0";
        ::grpc::ServerBuilder builder;
        int selected_port = 0;
        builder.AddListeningPort(server_address, ::grpc::InsecureServerCredentials(),
                                 &selected_port);
        builder.RegisterService(&service);
        server = builder.BuildAndStart();
        ASSERT_NE(server, nullptr);
        server_address = "localhost:" + std::to_string(selected_port);
    }

    class TmpDiscoveryFile {
      public:
        explicit TmpDiscoveryFile(const std::string& content) {
            std::string file_name = absl::StrCat(
                    "grpc_client_test_",
                    absl::Hex(absl::Uniform<uint64_t>(absl::BitGen()), absl::kSpacePad16));
            mPath = fs::temp_directory_path() / file_name;
            std::ofstream out(mPath);
            out << content;
        }

        ~TmpDiscoveryFile() {
            std::error_code ec;
            fs::remove(mPath, ec);
        }

        const fs::path& path() const { return mPath; }

      private:
        fs::path mPath;
    };

    MockEmulatorController service;
    std::unique_ptr<::grpc::Server> server;
    std::string server_address;
};

TEST_F(EmulatorClientTest, Connect_ValidDiscoveryFile_ConnectsSuccessfully) {
    StartServer();
    // Parse the port from server_address
    size_t colon = server_address.find(':');
    std::string port = server_address.substr(colon + 1);

    TmpDiscoveryFile tmpFile(absl::StrCat("grpc.port = ", port));
    EmulatorClient client(tmpFile.path().string());

    EXPECT_FALSE(client.IsConnected());
    absl::Status status = client.Connect(absl::Seconds(2));
    EXPECT_TRUE(status.ok()) << status.message();
    EXPECT_TRUE(client.IsConnected());

    client.Disconnect();
    EXPECT_FALSE(client.IsConnected());
}

TEST_F(EmulatorClientTest, StreamScreenshot_ReceivesFrames) {
    StartServer();
    size_t colon = server_address.find(':');
    std::string port = server_address.substr(colon + 1);

    TmpDiscoveryFile tmpFile(absl::StrCat("grpc.port = ", port));
    EmulatorClient client(tmpFile.path().string());
    ASSERT_TRUE(client.Connect(absl::Seconds(2)).ok());

    ::grpc::ClientContext context;
    ImageFormat format;
    format.set_display(2);

    auto reader = client.StreamScreenshot(&context, format);
    ASSERT_NE(reader, nullptr);

    Image frame;
    EXPECT_TRUE(reader->Read(&frame));
    EXPECT_EQ(frame.width(), 1280);
    EXPECT_EQ(frame.height(), 720);

    ::grpc::Status status = reader->Finish();
    EXPECT_TRUE(status.ok());
    EXPECT_TRUE(service.mScreenshotRequested);
    EXPECT_EQ(service.mRequestedDisplayId, 2);
}

TEST_F(EmulatorClientTest, StreamAudio_ReceivesAudioPackets) {
    StartServer();
    size_t colon = server_address.find(':');
    std::string port = server_address.substr(colon + 1);

    TmpDiscoveryFile tmpFile(absl::StrCat("grpc.port = ", port));
    EmulatorClient client(tmpFile.path().string());
    ASSERT_TRUE(client.Connect(absl::Seconds(2)).ok());

    ::grpc::ClientContext context;
    AudioFormat format;

    auto reader = client.StreamAudio(&context, format);
    ASSERT_NE(reader, nullptr);

    AudioPacket packet;
    EXPECT_TRUE(reader->Read(&packet));
    EXPECT_EQ(packet.audio(), "mock-pcm-data");

    ::grpc::Status status = reader->Finish();
    EXPECT_TRUE(status.ok());
    EXPECT_TRUE(service.mAudioRequested);
}

TEST_F(EmulatorClientTest, StreamInputEvent_SendsEvents) {
    StartServer();
    size_t colon = server_address.find(':');
    std::string port = server_address.substr(colon + 1);

    TmpDiscoveryFile tmpFile(absl::StrCat("grpc.port = ", port));
    EmulatorClient client(tmpFile.path().string());
    ASSERT_TRUE(client.Connect(absl::Seconds(2)).ok());

    ::grpc::ClientContext context;
    ::google::protobuf::Empty response;

    auto writer = client.StreamInputEvent(&context, &response);
    ASSERT_NE(writer, nullptr);

    InputEvent keyEvent;
    keyEvent.mutable_key_event()->set_key("A");
    EXPECT_TRUE(writer->Write(keyEvent));

    InputEvent mouseEvent;
    mouseEvent.mutable_mouse_event()->set_x(100);
    mouseEvent.mutable_mouse_event()->set_y(200);
    EXPECT_TRUE(writer->Write(mouseEvent));

    EXPECT_TRUE(writer->WritesDone());
    ::grpc::Status status = writer->Finish();
    EXPECT_TRUE(status.ok());

    EXPECT_TRUE(service.mEventStreamOpened);
    ASSERT_EQ(service.mReceivedEvents.size(), 2);
    EXPECT_EQ(service.mReceivedEvents[0].key_event().key(), "A");
    EXPECT_EQ(service.mReceivedEvents[1].mouse_event().x(), 100);
    EXPECT_EQ(service.mReceivedEvents[1].mouse_event().y(), 200);
}

}  // namespace
}  // namespace goldfish::videobridge
