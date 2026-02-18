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

#include "goldfish/async/event_loop.h"
#include "goldfish/avd_universe/battery/battery_state.h"

namespace goldfish::devices::battery {

/**
 * @brief Registers the battery device and connects it to the Universe.
 *
 * This function sets up a subscription to the provided ObservableBattery and
 * forwards all updates to the underlying QEMU hardware.
 *
 * @param observable_battery The source of battery state updates.
 * @param is_present The initial presence state of the battery.
 * @param qemu_loop The event loop on which QEMU updates should be performed.
 */
void RegisterBattery(avd_universe::battery::ObservableBattery* observable_battery, bool is_present,
                     async::EventLoop* qemu_loop);

}  // namespace goldfish::devices::battery
