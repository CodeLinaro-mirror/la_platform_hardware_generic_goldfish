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

#include "grpc_input_sender.h"

#include "absl/log/log.h"

namespace goldfish::videobridge {

GrpcInputSender::GrpcInputSender(std::shared_ptr<EmulatorClient> client)
        : client_(std::move(client)) {}

GrpcInputSender::~GrpcInputSender() {
    CloseWriter();
}

absl::Status GrpcInputSender::Start() {
    VLOG(1) << "Initializing GrpcInputSender event stream writer.";
    const absl::MutexLock lock(&writer_mutex_);
    if (writer_) {
        return absl::OkStatus();
    }
    if (!client_) {
        return absl::FailedPreconditionError("EmulatorClient is null.");
    }
    if (!client_->IsConnected()) {
        return absl::FailedPreconditionError("EmulatorClient is not connected to the emulator.");
    }
    writer_ = client_->StreamInputEvent(&context_, &response_);
    if (!writer_) {
        return absl::InternalError("Failed to open gRPC input event stream via EmulatorClient.");
    }
    LOG(INFO) << "Successfully opened emulator gRPC input event stream.";
    return absl::OkStatus();
}

void GrpcInputSender::SendEvent(const InputEvent& event) {
    VLOG(1) << "Sending InputEvent to emulator gRPC stream: " << event.ShortDebugString();
    const absl::MutexLock lock(&writer_mutex_);
    if (writer_) {
        if (!writer_->Write(event)) {
            LOG(ERROR) << "Failed to write InputEvent to emulator gRPC stream. Remote connection "
                          "may have closed.";
            writer_.reset();
        }
    } else {
        LOG(WARNING) << "Dropped incoming input event because the emulator gRPC stream is not "
                        "connected.";
    }
}

void GrpcInputSender::Stop() {
    CloseWriter();
}

void GrpcInputSender::CloseWriter() {
    const absl::MutexLock lock(&writer_mutex_);
    if (writer_) {
        writer_->WritesDone();
        writer_->Finish();
        writer_.reset();
        LOG(INFO) << "Closed emulator gRPC input event stream.";
    }
}

}  // namespace goldfish::videobridge
