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

#include "android/goldfish/hardware_config.h"
#include "emulator_controller.grpc.pb.h"
#include "goldfish/avd_info/avd_info.h"
#include "goldfish/avd_universe/guest_status/guest_status.h"

namespace android {
namespace emulation {
namespace control {

using ::goldfish::avd_info::AvdProperties;
using ::goldfish::avd_universe::guest_status::GuestStatus;
using grpc::Status;

class StatusServiceImpl {
  public:
    StatusServiceImpl(GuestStatus& guestStatus, const AvdProperties& avd_properties);

    Status getStatus(EmulatorStatus* reply);

  private:
    GuestStatus& guest_status_;
    const AvdProperties& avd_properties_;
};

}  // namespace control
}  // namespace emulation
}  // namespace android
