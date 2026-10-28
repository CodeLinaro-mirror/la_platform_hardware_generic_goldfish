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
 * InputSender implementation that forwards WebRTC DataChannel input and sensor events over gRPC.
 *
 * Manages the lifecycle of gRPC client streams (v1 EmulatorController.streamInputEvent and
 * v2 InputService.StreamInputEvents) as well as unary v2 SensorsService.UpdateSensorsState
 * requests, opening streams in Start(), forwarding events in Send*(), and closing streams in
 * Stop().
 *
 * Invariants: At most one gRPC stream writer (`writer_` or `v2_writer_`) is active for the
 * configured `label_` when `writer_mutex_` is not held, and each active writer has a matching
 * non-null `ClientContext` (`context_` or `v2_context_`).
 *
 * Thread Safety: Thread-safe. All stream writer mutations and writes are serialized under
 * `writer_mutex_`.
 *
 * Ownership & Lifetimes: Shares ownership of `EmulatorClient` via `std::shared_ptr` and owns
 * the underlying gRPC `ClientContext` and `ClientWriterInterface` instances.
 */
class GrpcInputSender : public InputSender {
  public:
    /**
     * @brief Constructs a GrpcInputSender.
     *
     * @param client The active gRPC EmulatorClient used to establish streams.
     * @param label The DataChannelLabel this sender services (defaults to kInput).
     */
    explicit GrpcInputSender(std::shared_ptr<EmulatorClient> client,
                             DataChannelLabel label = DataChannelLabel::kInput);
    ~GrpcInputSender() override;

    /**
     * @brief Opens the client streaming gRPC writer to the emulator controller or v2 input service.
     * Thread-safe.
     *
     * @return absl::Status indicating whether the gRPC writer stream was successfully opened.
     */
    absl::Status Start() override ABSL_LOCKS_EXCLUDED(writer_mutex_);

    /**
     * @brief Writes a parsed InputEvent directly to the gRPC client stream.
     * Thread-safe. Drops events if stream is not established.
     *
     * @param event The parsed InputEvent protobuf.
     */
    void SendEvent(const InputEvent& event) override ABSL_LOCKS_EXCLUDED(writer_mutex_);

    /**
     * @brief Writes a parsed v2 InputEvent directly to the v2 InputService client stream.
     * Thread-safe.
     *
     * @param event The parsed v2 InputEvent protobuf.
     */
    void SendV2Event(const V2InputEvent& event) override ABSL_LOCKS_EXCLUDED(writer_mutex_);

    /**
     * @brief Forwards a parsed v2 UpdateSensorsStateRequest to the v2 SensorsService.
     * Thread-safe.
     *
     * @param request The parsed v2 UpdateSensorsStateRequest protobuf.
     */
    void SendV2SensorsUpdate(const V2SensorsUpdateRequest& request) override;

    /**
     * @brief Closes the gRPC client stream writer and completes the call.
     * Thread-safe.
     */
    void Stop() override ABSL_LOCKS_EXCLUDED(writer_mutex_);

  private:
    void CloseWriter() ABSL_LOCKS_EXCLUDED(writer_mutex_);

    std::shared_ptr<EmulatorClient> client_;
    DataChannelLabel label_;
    absl::Mutex writer_mutex_;
    std::unique_ptr<::grpc::ClientContext> context_ ABSL_GUARDED_BY(writer_mutex_);
    ::google::protobuf::Empty response_ ABSL_GUARDED_BY(writer_mutex_);
    std::unique_ptr<::grpc::ClientWriterInterface<InputEvent>> writer_
            ABSL_GUARDED_BY(writer_mutex_);
    std::unique_ptr<::grpc::ClientContext> v2_context_ ABSL_GUARDED_BY(writer_mutex_);
    V2StreamInputEventsResponse v2_response_ ABSL_GUARDED_BY(writer_mutex_);
    std::unique_ptr<::grpc::ClientWriterInterface<V2InputEvent>> v2_writer_
            ABSL_GUARDED_BY(writer_mutex_);
};

}  // namespace goldfish::videobridge
