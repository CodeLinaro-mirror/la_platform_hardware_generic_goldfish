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

#include "android/emulation/control/ui_controller_forwarder.h"

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

#include "android/emulation/control/grpc_channel_factory.h"

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

namespace {

constexpr absl::string_view kUiControllerServiceUri = "android.emulation.control.UiController";

}  // namespace

UiControllerForwarder::UiControllerForwarder(std::shared_ptr<ServiceForwarderImpl> forwarder)
        : forwarder_(std::move(forwarder)) {}

template <typename Request, typename Response>
Status UiControllerForwarder::ForwardCall(
        ServerContext* context, const Request* request, Response* reply,
        Status (UiController::Stub::*method)(ClientContext*, const Request&, Response*)) {
    auto endpoint = forwarder_->GetEndpoint(std::string(kUiControllerServiceUri));
    if (!endpoint) {
        const std::string error_msg = absl::StrCat(
                "Cannot forward UiController RPC: no forwarding rule is registered for service "
                "URI '",
                kUiControllerServiceUri,
                "'. Register a forwarding endpoint via ServiceForwarder.registerForwarder before "
                "invoking UiController RPCs.");
        LOG(ERROR) << error_msg;
        return Status(StatusCode::UNIMPLEMENTED, error_msg);
    }

    VLOG(1) << "Forwarding UiController call to target '" << endpoint->target() << "'";
    // TODO: We might want to cache the channel/stub if creating it every time is too expensive.
    // For infrequent UI calls, this is fine.
    android::emulation::control::GrpcChannelFactory factory(*endpoint, {});
    auto channel = factory.CreateChannel();
    if (!channel) {
        const std::string error_msg = absl::StrCat(
                "Failed to create gRPC forwarding channel for service URI '",
                kUiControllerServiceUri, "' to target '", endpoint->target(),
                "': target must be a localhost/127.0.0.1 address or provide TLS credentials. "
                "Register a valid endpoint via ServiceForwarder.registerForwarder.");
        LOG(ERROR) << error_msg;
        return Status(StatusCode::FAILED_PRECONDITION, error_msg);
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
