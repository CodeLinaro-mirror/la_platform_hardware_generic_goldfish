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

#pragma once

#include <mutex>
#include <utility>

#include "absl/container/flat_hash_map.h"

#include "goldfish/eventing/observable_value.h"
#include "vehicle_service.pb.h"

namespace goldfish::avd_universe::vehicle {

using ::android::emulation::control::incubating::VehiclePropValue;

using ObservableVehiclePropValue =
        eventing::ObservableValue<VehiclePropValue, eventing::ObservableValueTriggerAlways>;

struct VehicleChannel {
    ObservableVehiclePropValue host_to_guest;
    ObservableVehiclePropValue guest_to_host;
    mutable std::mutex guest_state_mutex;
    absl::flat_hash_map<std::pair<int32_t, int32_t>, VehiclePropValue> guest_state_map;
};

}  // namespace goldfish::avd_universe::vehicle
