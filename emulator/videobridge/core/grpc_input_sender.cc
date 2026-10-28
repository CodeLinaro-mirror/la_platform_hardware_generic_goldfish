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

#include <string_view>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

namespace goldfish::videobridge {
namespace {

template <typename Writer, typename Response, typename OpenFn>
absl::Status OpenStream(std::unique_ptr<Writer>& writer,
                        std::unique_ptr<::grpc::ClientContext>& context, Response& response,
                        OpenFn&& open_fn, std::string_view version) {
    if (writer) {
        return absl::OkStatus();
    }
    context = std::make_unique<::grpc::ClientContext>();
    writer = open_fn(context.get(), &response);
    if (!writer) {
        context.reset();
        return absl::InternalError(absl::StrCat("Failed to open ", version,
                                                " gRPC input event stream via EmulatorClient."));
    }
    LOG(INFO) << "Successfully opened emulator " << version << " gRPC input event stream.";
    return absl::OkStatus();
}

template <typename Writer>
void CloseStream(std::unique_ptr<Writer>& writer, std::unique_ptr<::grpc::ClientContext>& context,
                 std::string_view version) {
    if (!writer) {
        return;
    }
    if (context) {
        context->TryCancel();
    }
    writer->Finish();
    writer.reset();
    context.reset();
    LOG(INFO) << "Closed emulator " << version << " gRPC input event stream.";
}

template <typename Writer, typename Event>
void WriteStreamEvent(std::unique_ptr<Writer>& writer,
                      std::unique_ptr<::grpc::ClientContext>& context, const Event& event,
                      std::string_view version) {
    VLOG(1) << "Sending " << version
            << " InputEvent to emulator gRPC stream: " << event.ShortDebugString();
    if (!writer) {
        LOG(WARNING) << "Dropped incoming " << version << " input event because the " << version
                     << " gRPC stream is not connected.";
        return;
    }
    if (!writer->Write(event)) {
        LOG(ERROR) << "Failed to write " << version
                   << " InputEvent to emulator gRPC stream. Remote connection may have closed.";
        CloseStream(writer, context, version);
    }
}

}  // namespace

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
        return OpenStream(
                writer_, context_, response_,
                [this](::grpc::ClientContext* context, ::google::protobuf::Empty* response) {
                    return client_->StreamInputEvent(context, response);
                },
                "v1");
    }
    if (label_ == DataChannelLabel::kInputV2) {
        return OpenStream(
                v2_writer_, v2_context_, v2_response_,
                [this](::grpc::ClientContext* context, V2StreamInputEventsResponse* response) {
                    return client_->StreamV2InputEvents(context, response);
                },
                "v2");
    }
    return absl::OkStatus();
}

void GrpcInputSender::SendEvent(const InputEvent& event) {
    const absl::MutexLock writer_lock(&writer_mutex_);
    WriteStreamEvent(writer_, context_, event, "v1");
}

void GrpcInputSender::SendV2Event(const V2InputEvent& event) {
    const absl::MutexLock writer_lock(&writer_mutex_);
    WriteStreamEvent(v2_writer_, v2_context_, event, "v2");
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
    CloseStream(writer_, context_, "v1");
    CloseStream(v2_writer_, v2_context_, "v2");
}

}  // namespace goldfish::videobridge
