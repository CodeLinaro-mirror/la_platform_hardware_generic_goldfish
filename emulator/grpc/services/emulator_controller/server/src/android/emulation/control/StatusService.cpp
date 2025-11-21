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
#include "android/emulation/control/StatusService.h"

#include "absl/log/log.h"

#include "android/base/system/System.h"
#include "android/goldfish/config/hardware_config.h"
#include "android/misc/GuestStatusDevice.h"

namespace android {
namespace emulation {
namespace control {

using android::base::System;
using ::goldfish::devices::ConnectorRegistry;
using ::goldfish::devices::guest_status::IGuestStatusDevice;

std::unordered_map<std::string, std::string> getQemuConfig(int api_level, const android::goldfish::HardwareConfig &hw) {
    std::unordered_map<std::string, std::string> cfg;

    /* use the magic of macros to implement the hardware configuration loaded */
#define HWCFG_BOOL(n, s, d, a, t) cfg[s] = hw.n ? "true" : "false";
#define HWCFG_INT(n, s, d, a, t) cfg[s] = std::to_string(hw.n);
#define HWCFG_STRING(n, s, d, a, t) cfg[s] = hw.n;
#define HWCFG_DOUBLE(n, s, d, a, t) cfg[s] std::to_string(hw.n);
#define HWCFG_DISKSIZE(n, s, d, a, t) cfg[s] = hw.n.string();

#include "avd/hw-config-defs.h"

    cfg["avd.api_level"] = std::to_string(api_level);

    return cfg;
}

StatusServiceImpl::StatusServiceImpl(ConnectorRegistry* connectorRegistry, int api_level, const android::goldfish::HardwareConfig &hw)
    : mRegistry(connectorRegistry), mApiLevel(api_level), mHw(hw) {}

grpc::Status StatusServiceImpl::getStatus(EmulatorStatus* reply) {
    // TODO(jansene): Get cpu count, hypervisor type.`
    reply->set_uptime(System::get()->getProcessTimes().wallClockMs);

    auto weak = mRegistry->activeDevice<IGuestStatusDevice>();
    if (auto status = weak.lock()) {
        VLOG(1) << "Getting status: " << status->hasBooted();
        VLOG(1) << "Heartbeat: " << status->heartbeat();
        reply->set_booted(status->hasBooted());
        reply->set_heartbeat(status->heartbeat());
    }

    auto cnf = getQemuConfig(mApiLevel, mHw);

    auto entries = reply->mutable_hardwareconfig();
    for (const auto& entry : cnf) {
        auto response_entry = entries->add_entry();
        response_entry->set_key(entry.first);
        response_entry->set_value(entry.second);
    };

    // TODO(jansene): Enable once multidisplay support is added.
    (*reply->mutable_guestconfig())["multidisplay"] = "unavailable";

    return grpc::Status::OK;
}

}  // namespace control
}  // namespace emulation
}  // namespace android
