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
#pragma once

#include <memory>
#include <string>

#include "android/emulation/forwarding/service_forwarder_impl.h"
#include "ui_controller_service.grpc.pb.h"

namespace android::emulation::forwarding {

class UiControllerForwarder final : public android::emulation::control::UiController::Service {
  public:
    explicit UiControllerForwarder(std::shared_ptr<ServiceForwarderImpl> forwarder);

    grpc::Status showExtendedControls(
            grpc::ServerContext* context, const android::emulation::control::PaneEntry* request,
            android::emulation::control::ExtendedControlsStatus* reply) override;

    grpc::Status closeExtendedControls(
            grpc::ServerContext* context, const google::protobuf::Empty* request,
            android::emulation::control::ExtendedControlsStatus* reply) override;

    grpc::Status setUiTheme(grpc::ServerContext* context,
                            const android::emulation::control::ThemingStyle* request,
                            google::protobuf::Empty* reply) override;

    grpc::Status getUserConfig(grpc::ServerContext* context, const google::protobuf::Empty* request,
                               android::emulation::control::UserConfig* reply) override;

  private:
    template <typename Request, typename Response>
    grpc::Status ForwardCall(
            grpc::ServerContext* context, const Request* request, Response* reply,
            grpc::Status (android::emulation::control::UiController::Stub::*method)(
                    grpc::ClientContext*, const Request&, Response*));

    std::shared_ptr<ServiceForwarderImpl> forwarder_;
};

}  // namespace android::emulation::forwarding
