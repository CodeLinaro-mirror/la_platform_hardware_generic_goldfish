// Copyright 2026 The Android Open Source Project
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

#include "android/emulation/forwarding/ui_controller_forwarder.h"

#include "absl/log/log.h"

#include "emulator/grpc/client/grpc_channel_factory.h"

namespace android::emulation::forwarding {

using android::emulation::control::ExtendedControlsStatus;
using android::emulation::control::PaneEntry;
using android::emulation::control::ThemingStyle;
using android::emulation::control::UiController;
using android::emulation::control::UserConfig;
using ::google::protobuf::Empty;
using ::grpc::ClientContext;
using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;

UiControllerForwarder::UiControllerForwarder(std::shared_ptr<ServiceForwarderImpl> forwarder)
        : forwarder_(std::move(forwarder)) {}

template <typename Request, typename Response>
Status UiControllerForwarder::ForwardCall(
        ServerContext* context, const Request* request, Response* reply,
        Status (UiController::Stub::*method)(ClientContext*, const Request&, Response*)) {
    auto endpoint_opt = forwarder_->GetEndpoint("android.emulation.control.UiController");
    if (!endpoint_opt) {
        LOG(ERROR) << "No forwarding rule found for android.emulation.control.UiController";
        return Status(StatusCode::UNIMPLEMENTED, "No forwarding rule registered");
    }

    VLOG(1) << "Forwarding UiController call to " << endpoint_opt->ShortDebugString();
    // TODO: We might want to cache the channel/stub if creating it every time is too expensive.
    // For infrequent UI calls, this is fine.
    android::emulation::control::GrpcChannelFactory factory(*endpoint_opt, {});
    auto channel = factory.CreateChannel();
    if (!channel) {
        return Status(StatusCode::INTERNAL, "Failed to create forward channel");
    }

    auto stub = UiController::NewStub(channel);

    ClientContext client_context;
    if (context) {
        client_context.set_deadline(context->deadline());
    }

    return (stub.get()->*method)(&client_context, *request, reply);
}

Status UiControllerForwarder::showExtendedControls(ServerContext* context, const PaneEntry* request,
                                                   ExtendedControlsStatus* reply) {
    return ForwardCall(context, request, reply, &UiController::Stub::showExtendedControls);
}

Status UiControllerForwarder::closeExtendedControls(ServerContext* context, const Empty* request,
                                                    ExtendedControlsStatus* reply) {
    return ForwardCall(context, request, reply, &UiController::Stub::closeExtendedControls);
}

Status UiControllerForwarder::setUiTheme(ServerContext* context, const ThemingStyle* request,
                                         Empty* reply) {
    return ForwardCall(context, request, reply, &UiController::Stub::setUiTheme);
}

Status UiControllerForwarder::getUserConfig(ServerContext* context, const Empty* request,
                                            UserConfig* reply) {
    return ForwardCall(context, request, reply, &UiController::Stub::getUserConfig);
}

}  // namespace android::emulation::forwarding
