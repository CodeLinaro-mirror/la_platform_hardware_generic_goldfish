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

#include "android/emulation/control/absl_status_translate.h"

namespace goldfish::videobridge {

EmulatorClient::EmulatorClient(std::string discovery_file)
        : discovery_file_(std::move(discovery_file)) {}

EmulatorClient::~EmulatorClient() {
    Disconnect();
}

absl::Status EmulatorClient::Connect(absl::Duration timeout) {
    auto built_client =
            discovery_file_.empty()
                    ? EmulatorGrpcClientBuilder().ForDiscoveredEmulator({}).BuildBlocking()
                    : EmulatorGrpcClientBuilder()
                              .WithDiscoveryFile(discovery_file_)
                              .BuildBlocking();

    if (!built_client.ok()) {
        LOG(ERROR) << "Failed to build EmulatorGrpcClient: " << built_client.status();
        return built_client.status();
    }
    auto client = std::move(*built_client);

    absl::Status status = client->Connect(timeout);
    if (!status.ok()) {
        LOG(ERROR) << "Failed to connect to emulator: " << status;
        return status;
    }

    auto controller_stub = client->Stub<EmulatorController>();
    if (!controller_stub.ok()) {
        LOG(ERROR) << "Failed to create EmulatorController stub: " << controller_stub.status();
        return controller_stub.status();
    }

    auto input_stub = client->Stub<V2InputService>();
    if (!input_stub.ok()) {
        LOG(ERROR) << "Failed to create V2 InputService stub: " << input_stub.status();
        return input_stub.status();
    }

    auto sensors_stub = client->Stub<V2SensorsService>();
    if (!sensors_stub.ok()) {
        LOG(ERROR) << "Failed to create V2 SensorsService stub: " << sensors_stub.status();
        return sensors_stub.status();
    }

    grpc_client_ = std::move(client);
    stub_ = std::move(*controller_stub);
    v2_input_stub_ = std::move(*input_stub);
    v2_sensors_stub_ = std::move(*sensors_stub);
    LOG(INFO) << "Connected to emulator gRPC server at " << TargetAddress();
    return absl::OkStatus();
}

void EmulatorClient::Disconnect() {
    v2_sensors_stub_ = nullptr;
    v2_input_stub_ = nullptr;
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

std::unique_ptr<::grpc::ClientWriterInterface<V2InputEvent>> EmulatorClient::StreamV2InputEvents(
        ::grpc::ClientContext* context, V2StreamInputEventsResponse* response) {
    if (!IsConnected() || !v2_input_stub_) {
        LOG(ERROR) << "EmulatorClient is not connected for v2 input events.";
        return nullptr;
    }
    return v2_input_stub_->StreamInputEvents(context, response);
}

absl::Status EmulatorClient::UpdateV2SensorsState(const V2SensorsUpdateRequest& request) {
    if (!IsConnected() || !v2_sensors_stub_) {
        return absl::FailedPreconditionError("EmulatorClient is not connected for v2 sensors.");
    }
    ::grpc::ClientContext context;
    ::android::emulation::v2::SensorsState response;
    const ::grpc::Status status =
            v2_sensors_stub_->UpdateSensorsState(&context, request, &response);
    return ::android::emulation::control::GrpcStatusToAbslStatus(status);
}

}  // namespace goldfish::videobridge
