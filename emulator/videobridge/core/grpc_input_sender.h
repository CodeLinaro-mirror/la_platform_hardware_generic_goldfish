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
// Disable compiler warnings for external third-party headers. We wrap these in localized
// pragma blocks rather than using target 'copts' so that thread-safety analysis remains
// active on our own local source files.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-reference-return"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "grpcpp/grpcpp.h"
#pragma clang diagnostic pop

#include <memory>

#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"

#include "goldfish/videobridge/emulator_client.h"
#include "goldfish/videobridge/input_sender.h"

namespace goldfish::videobridge {

using ::android::emulation::control::InputEvent;

/**
 * @class GrpcInputSender
 * @brief An InputSender implementation that forwards input events over a gRPC client stream.
 *
 * It manages the lifecycle of the gRPC ClientWriter, opening the stream when Start()
 * is called, writing events thread-safely in SendEvent(), and closing the stream in Stop().
 * This class is designed to be injected into EventForwarder, which manages the WebRTC
 * data channel lifecycle.
 *
 * Thread-safe.
 */
class GrpcInputSender : public InputSender {
  public:
    /**
     * @brief Constructs a GrpcInputSender.
     *
     * @param client The active gRPC EmulatorClient used to establish streams. Must outlive this
     * sender.
     */
    explicit GrpcInputSender(std::shared_ptr<EmulatorClient> client);
    ~GrpcInputSender() override;

    /**
     * @brief Opens the client streaming gRPC writer to the emulator controller.
     * Thread-safe.
     *
     * @return absl::Status indicating whether the gRPC writer stream was successfully opened.
     */
    absl::Status Start() override;

    /**
     * @brief Writes a parsed InputEvent directly to the gRPC client stream.
     * Thread-safe. Drops events if stream is not established.
     *
     * @param event The parsed InputEvent protobuf.
     */
    void SendEvent(const InputEvent& event) override;

    /**
     * @brief Closes the gRPC client stream writer and completes the call.
     * Thread-safe.
     */
    void Stop() override;

  private:
    void CloseWriter();

    std::shared_ptr<EmulatorClient> client_;
    absl::Mutex writer_mutex_;
    ::grpc::ClientContext context_;
    ::google::protobuf::Empty response_;
    std::unique_ptr<::grpc::ClientWriterInterface<InputEvent>> writer_
            ABSL_GUARDED_BY(writer_mutex_);
};

}  // namespace goldfish::videobridge
