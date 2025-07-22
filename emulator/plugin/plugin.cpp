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

#include <cstdio>
#include "goldfish/avd/avd-info.h"
#include "goldfish/avd/avd-finalize.h"
#include "goldfish/avd/adb-device.h"
#include "goldfish/vsock/vsock_port_fwd.h"
#include "goldfish/vsock/vsock_low_level.h"
#include "goldfish/battery/goldfish_battery.h"
#include "goldfish/input/virtio-input-android.h"
#ifndef _WIN32
#include "goldfish/grpc/grpc-service-device.h"
#endif

extern "C" void goldfish_register_types(void) {
    goldfish_battery_register_types();
    vsock_port_fwd_register_types();
    vsock_low_level_register_types();
    goldfish::avd_info::avd_info_register_types();
    goldfish::avd_finalize::avd_finalize_register_types();
    goldfish::adb_device::adb_device_register_types();
    virtio_input_android_register_types();
#ifndef _WIN32
    grpc_register_types();
#endif
}
