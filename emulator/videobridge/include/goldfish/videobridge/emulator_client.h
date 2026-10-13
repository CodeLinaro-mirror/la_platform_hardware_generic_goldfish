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
#pragma once

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"

#include "android/emulation/control/emulator_grpc_client.h"
#include "emulator_controller.grpc.pb.h"
#include "input_service.grpc.pb.h"
#include "sensors_service.grpc.pb.h"

namespace goldfish::videobridge {

using ::android::emulation::control::AudioFormat;
using ::android::emulation::control::AudioPacket;
using ::android::emulation::control::BlockingEmulatorGrpcClient;
using ::android::emulation::control::ConnectionState;
using ::android::emulation::control::EmulatorController;
using ::android::emulation::control::EmulatorGrpcClientBuilder;
using ::android::emulation::control::Image;
using ::android::emulation::control::ImageFormat;
using ::android::emulation::control::InputEvent;
using V2InputEvent = ::android::emulation::v2::InputEvent;
using V2InputService = ::android::emulation::v2::InputService;
using V2SensorsService = ::android::emulation::v2::SensorsService;
using V2SensorsUpdateRequest = ::android::emulation::v2::UpdateSensorsStateRequest;
using V2StreamInputEventsResponse = ::android::emulation::v2::StreamInputEventsResponse;

/**
 * Client manager wrapping EmulatorGrpcClient to stream screenshots, audio, and input/sensor
 * events (legacy v1 and v2).
 *
 * This class auto-discovers running Android Emulator instances using platform INI discovery
 * directories, constructs the gRPC channel, and provides streaming and unary stub helpers for
 * remote desktop services.
 *
 * Thread Safety: Connect() and Disconnect() are not thread-safe and must not be called
 * concurrently with RPC methods. Once connected, Stream* and Update* methods are thread-safe.
 *
 * Ownership & Lifetimes: Owns the underlying gRPC channel and service stubs. Callers of
 * Stream* methods own the returned ClientReaderInterface/ClientWriterInterface and must ensure
 * the passed grpc::ClientContext outlives the stream.
 */
class EmulatorClient {
  public:
    /**
     * @brief Constructs an EmulatorClient.
     *
     * @param discovery_file Optional absolute path to a specific emulator registration discovery
     * INI file. If empty, auto-discovery will scan the default discovery directory.
     */
    explicit EmulatorClient(std::string discovery_file = "");
    virtual ~EmulatorClient();

    /**
     * @brief Establishes a gRPC channel and verifies connectivity with the emulator controller.
     *
     * @param timeout The connection timeout duration. Defaults to 5 seconds.
     * @return absl::Status OK if connected, deadline_exceeded or unavailable status on failure.
     */
    virtual absl::Status Connect(absl::Duration timeout = absl::Seconds(5));

    /**
     * @brief Disconnects the gRPC channel and destroys the controller stub.
     */
    virtual void Disconnect();

    /**
     * @brief Returns whether the client is currently connected.
     */
    virtual bool IsConnected() const;

    /**
     * @brief Returns the gRPC target address of the connected emulator, or empty string.
     */
    virtual std::string TargetAddress() const;

    /**
     * @brief Initiates a server-streaming RPC call to stream screenshots from the emulator.
     *
     * @param context Pre-allocated gRPC ClientContext managing the lifetime of this call.
     * @param format Screenshot settings (like resolution, format, and display ID).
     * @return std::unique_ptr<::grpc::ClientReaderInterface<Image>> Reader stream, or nullptr if
     * not connected.
     */
    virtual std::unique_ptr<::grpc::ClientReaderInterface<Image>> StreamScreenshot(
            ::grpc::ClientContext* context, const ImageFormat& format);

    /**
     * @brief Initiates a server-streaming RPC call to stream audio packets from the emulator.
     *
     * @param context Pre-allocated gRPC ClientContext managing the lifetime of this call.
     * @param format Audio configuration settings.
     * @return std::unique_ptr<::grpc::ClientReaderInterface<AudioPacket>> Reader stream, or nullptr
     * if not connected.
     */
    virtual std::unique_ptr<::grpc::ClientReaderInterface<AudioPacket>> StreamAudio(
            ::grpc::ClientContext* context, const AudioFormat& format);

    /**
     * @brief Initiates a client-streaming RPC call to forward user input events to the emulator.
     *
     * @param context Pre-allocated gRPC ClientContext managing the lifetime of this call.
     * @param response Protobuf Empty object populated once the stream completes.
     * @return std::unique_ptr<::grpc::ClientWriterInterface<InputEvent>> Writer stream, or nullptr
     * if not connected.
     */
    virtual std::unique_ptr<::grpc::ClientWriterInterface<InputEvent>> StreamInputEvent(
            ::grpc::ClientContext* context, ::google::protobuf::Empty* response);

    /**
     * @brief Initiates a client-streaming RPC call to forward v2 input events to InputService.
     *
     * @param context Pre-allocated gRPC ClientContext managing the lifetime of this call.
     * @param response Protobuf StreamInputEventsResponse populated once the stream completes.
     * @return std::unique_ptr<::grpc::ClientWriterInterface<V2InputEvent>> Writer stream, or
     * nullptr if not connected.
     */
    virtual std::unique_ptr<::grpc::ClientWriterInterface<V2InputEvent>> StreamV2InputEvents(
            ::grpc::ClientContext* context, V2StreamInputEventsResponse* response);

    /**
     * @brief Sends a unary v2 sensor and pose update to SensorsService.
     *
     * @param request The v2 UpdateSensorsStateRequest payload.
     * @return absl::Status OK if applied, or error status if disconnected/failed.
     */
    virtual absl::Status UpdateV2SensorsState(const V2SensorsUpdateRequest& request);

  private:
    std::string discovery_file_;
    std::unique_ptr<BlockingEmulatorGrpcClient> grpc_client_;
    std::unique_ptr<EmulatorController::Stub> stub_;
    std::unique_ptr<V2InputService::Stub> v2_input_stub_;
    std::unique_ptr<V2SensorsService::Stub> v2_sensors_stub_;
};

}  // namespace goldfish::videobridge
