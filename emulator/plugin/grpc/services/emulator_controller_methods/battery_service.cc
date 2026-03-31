// Copyright (C) 2026  The Android Open Source Project
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
#include "battery_service.h"

namespace android::emulation::control {

using grpc::Status;
using UniverseBattery = ::goldfish::avd_universe::battery::Battery;

namespace {
UniverseBattery ProtoToBattery(const BatteryState& proto) {
    return {
        .has_battery = proto.hasbattery(),
        .is_present = proto.ispresent(),
        .charger = static_cast<UniverseBattery::Charger>(proto.charger()),
        .charge_level = proto.chargelevel(),
        .health = static_cast<UniverseBattery::Health>(proto.health()),
        .status = static_cast<UniverseBattery::Status>(proto.status()),
    };
}

BatteryState BatteryToProto(const UniverseBattery& state) {
    BatteryState proto;
    proto.set_hasbattery(state.has_battery);
    proto.set_ispresent(state.is_present);
    proto.set_charger(static_cast<BatteryState_BatteryCharger>(state.charger));
    proto.set_chargelevel(state.charge_level);
    proto.set_health(static_cast<BatteryState_BatteryHealth>(state.health));
    proto.set_status(static_cast<BatteryState_BatteryStatus>(state.status));
    return proto;
}
}  // namespace

Status BatteryServiceImpl::setBattery(const BatteryState& request) {
    observable_batttery_.SetValue(ProtoToBattery(request));
    return Status::OK;
}

Status BatteryServiceImpl::getBattery(BatteryState* reply) {
    *reply = BatteryToProto(observable_batttery_.GetValue());
    return Status::OK;
}

}  // namespace android::emulation::control
