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

GrpcInputSender::GrpcInputSender(std::shared_ptr<EmulatorClient> client, DataChannelLabel label)
        : client_(std::move(client)), label_(label) {}

GrpcInputSender::~GrpcInputSender() {
    CloseWriter();
}

absl::Status GrpcInputSender::Start() {
    VLOG(1) << "Initializing GrpcInputSender for label: " << AsString(label_);
    const absl::MutexLock writer_lock(&writer_mutex_);
    if (!client_) {
        return absl::FailedPreconditionError("EmulatorClient is null.");
    }
    if (!client_->IsConnected()) {
        return absl::FailedPreconditionError("EmulatorClient is not connected to the emulator.");
    }
    if (label_ == DataChannelLabel::kInput) {
        if (writer_) {
            return absl::OkStatus();
        }
        context_ = std::make_unique<::grpc::ClientContext>();
        writer_ = client_->StreamInputEvent(context_.get(), &response_);
        if (!writer_) {
            context_.reset();
            return absl::InternalError(
                    "Failed to open gRPC input event stream via EmulatorClient.");
        }
        LOG(INFO) << "Successfully opened emulator v1 gRPC input event stream.";
    } else if (label_ == DataChannelLabel::kInputV2) {
        if (v2_writer_) {
            return absl::OkStatus();
        }
        v2_context_ = std::make_unique<::grpc::ClientContext>();
        v2_writer_ = client_->StreamV2InputEvents(v2_context_.get(), &v2_response_);
        if (!v2_writer_) {
            v2_context_.reset();
            return absl::InternalError(
                    "Failed to open v2 gRPC input event stream via EmulatorClient.");
        }
        LOG(INFO) << "Successfully opened emulator v2 gRPC input event stream.";
    }
    return absl::OkStatus();
}

void GrpcInputSender::SendEvent(const InputEvent& event) {
    VLOG(1) << "Sending InputEvent to emulator gRPC stream: " << event.ShortDebugString();
    const absl::MutexLock writer_lock(&writer_mutex_);
    if (writer_) {
        if (!writer_->Write(event)) {
            LOG(ERROR) << "Failed to write InputEvent to emulator gRPC stream. Remote connection "
                          "may have closed.";
            CloseV1StreamLocked();
        }
    } else {
        LOG(WARNING) << "Dropped incoming input event because the emulator gRPC stream is not "
                        "connected.";
    }
}

void GrpcInputSender::SendV2Event(const V2InputEvent& event) {
    VLOG(1) << "Sending v2 InputEvent to emulator gRPC stream: " << event.ShortDebugString();
    const absl::MutexLock writer_lock(&writer_mutex_);
    if (v2_writer_) {
        if (!v2_writer_->Write(event)) {
            LOG(ERROR) << "Failed to write v2 InputEvent to emulator gRPC stream. Remote "
                          "connection may have closed.";
            CloseV2StreamLocked();
        }
    } else {
        LOG(WARNING) << "Dropped incoming v2 input event because the v2 gRPC stream is not "
                        "connected.";
    }
}

void GrpcInputSender::SendV2SensorsUpdate(const V2SensorsUpdateRequest& request) {
    VLOG(1) << "Sending v2 SensorsUpdate to emulator: " << request.ShortDebugString();
    if (!client_) {
        return;
    }
    if (const absl::Status status = client_->UpdateV2SensorsState(request); !status.ok()) {
        LOG(WARNING) << "Failed to forward v2 sensors update over gRPC: " << status;
    }
}

void GrpcInputSender::Stop() {
    CloseWriter();
}

void GrpcInputSender::CloseWriter() {
    const absl::MutexLock writer_lock(&writer_mutex_);
    CloseV1StreamLocked();
    CloseV2StreamLocked();
}

void GrpcInputSender::CloseV1StreamLocked() {
    if (!writer_) {
        return;
    }
    if (context_) {
        context_->TryCancel();
    }
    writer_->Finish();
    writer_.reset();
    context_.reset();
    LOG(INFO) << "Closed emulator v1 gRPC input event stream.";
}

void GrpcInputSender::CloseV2StreamLocked() {
    if (!v2_writer_) {
        return;
    }
    if (v2_context_) {
        v2_context_->TryCancel();
    }
    v2_writer_->Finish();
    v2_writer_.reset();
    v2_context_.reset();
    LOG(INFO) << "Closed emulator v2 gRPC input event stream.";
}

}  // namespace goldfish::videobridge
