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

#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"

namespace goldfish::videobridge {

EmulatorClient::EmulatorClient(std::string discovery_file)
        : discovery_file_(std::move(discovery_file)) {}

EmulatorClient::~EmulatorClient() {
    Disconnect();
}

absl::Status EmulatorClient::Connect(absl::Duration timeout) {
    auto client_or = discovery_file_.empty()
                             ? EmulatorGrpcClientBuilder().ForDiscoveredEmulator({}).BuildBlocking()
                             : EmulatorGrpcClientBuilder()
                                       .WithDiscoveryFile(discovery_file_)
                                       .BuildBlocking();

    if (!client_or.ok()) {
        LOG(ERROR) << "Failed to build EmulatorGrpcClient: " << client_or.status();
        return client_or.status();
    }
    auto client = std::move(*client_or);

    absl::Status status = client->Connect(timeout);
    if (!status.ok()) {
        LOG(ERROR) << "Failed to connect to emulator: " << status;
        return status;
    }

    auto stub_or = client->Stub<EmulatorController>();
    if (!stub_or.ok()) {
        LOG(ERROR) << "Failed to create EmulatorController stub: " << stub_or.status();
        return stub_or.status();
    }

    grpc_client_ = std::move(client);
    stub_ = std::move(*stub_or);
    LOG(INFO) << "Connected to emulator gRPC server at " << TargetAddress();
    return absl::OkStatus();
}

void EmulatorClient::Disconnect() {
    stub_ = nullptr;
    grpc_client_ = nullptr;
}

bool EmulatorClient::IsConnected() const {
    return stub_ != nullptr && grpc_client_ != nullptr &&
           grpc_client_->GetConnectionState() == ConnectionState::kConnected;
}

std::string EmulatorClient::TargetAddress() const {
    if (grpc_client_) {
        return grpc_client_->GetEndpoint().target();
    }
    return "<not connected>";
}

std::unique_ptr<::grpc::ClientReaderInterface<Image>> EmulatorClient::StreamScreenshot(
        ::grpc::ClientContext* context, const ImageFormat& format) {
    if (!IsConnected()) {
        LOG(ERROR) << "EmulatorClient is not connected.";
        return nullptr;
    }
    return stub_->streamScreenshot(context, format);
}

std::unique_ptr<::grpc::ClientReaderInterface<AudioPacket>> EmulatorClient::StreamAudio(
        ::grpc::ClientContext* context, const AudioFormat& format) {
    if (!IsConnected()) {
        LOG(ERROR) << "EmulatorClient is not connected.";
        return nullptr;
    }
    return stub_->streamAudio(context, format);
}

std::unique_ptr<::grpc::ClientWriterInterface<InputEvent>> EmulatorClient::StreamInputEvent(
        ::grpc::ClientContext* context, ::google::protobuf::Empty* response) {
    if (!IsConnected()) {
        LOG(ERROR) << "EmulatorClient is not connected.";
        return nullptr;
    }
    return stub_->streamInputEvent(context, response);
}

}  // namespace goldfish::videobridge
