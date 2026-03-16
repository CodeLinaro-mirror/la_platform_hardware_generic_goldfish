// Copyright 2024 The Android Open Source Project
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
#include <string_view>

#include "goldfish/avd_universe/grpc/grpc_notification_channel.h"
#include "goldfish/avd_universe/guest_status/guest_status.h"
#include "goldfish/devices/connector_registry.h"
#include "goldfish/devices/emulator_reset.h"

namespace goldfish::devices::guest_status {

using goldfish::async::EventLoop;
using goldfish::avd_universe::grpc::GrpcNotificationEventSource;
using goldfish::avd_universe::guest_status::GuestStatus;
using namespace std::string_view_literals;

/**
 * @brief Provides status information from the Android guest.
 */
class IGuestStatusDevice : public HalPlug {
  public:
    static constexpr std::string_view kServiceName = "QemuMiscPipe"sv;

    static void RegisterDevice(GuestStatus* guestStatus, GrpcNotificationEventSource* notificationSource,
                               IConnectorRegistry* registry, EmulatorResetCallbacks resetCallbacks,
                               EventLoop* client_loop, EventLoop* qemu_loop,
                               int quitAfterBootTimeoutSeconds);
};
}  // namespace goldfish::devices::guest_status