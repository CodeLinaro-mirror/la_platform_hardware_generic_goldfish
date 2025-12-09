// Copyright (C) 2024 The Android Open Source Project
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

#include <grpcpp/grpcpp.h>

#include "emulator_controller.grpc.pb.h"
#include "goldfish/display/MultiDisplay.h"
#include "goldfish/sensors/PhysicalModel.h"

namespace android {
namespace emulation {
namespace control {

using ::google::protobuf::Empty;
using grpc::ServerContext;
using grpc::Status;

/**
 * @brief Implements the display & multidisplay gRPC APIs.
 */
class DisplayServiceImpl : public EmulatorController::Service {
  public:
    DisplayServiceImpl(::goldfish::display::IMultiDisplay* display,
                       ::goldfish::sensors::PhysicalModel* pm)
            : mMultiDisplay(*display), mPhysicalModel(*pm) {}

    Status streamScreenshot(ServerContext* context, const ImageFormat* request,
                            grpc::ServerWriter<Image>* writer) override;

    Status getScreenshot(ServerContext* context, const ImageFormat* request, Image* reply) override;

    Status getDisplayConfigurations(ServerContext* context, const Empty* request,
                                    DisplayConfigurations* reply) override;

  private:
    static Status getDisplayConfigurations(const ::goldfish::display::IMultiDisplay& multiDisplay,
                                           DisplayConfigurations* reply);

    ::goldfish::display::IMultiDisplay& mMultiDisplay;
    ::goldfish::sensors::PhysicalModel& mPhysicalModel;
};

}  // namespace control
}  // namespace emulation
}  // namespace android
