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

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/internal/globals.h"
#include "absl/log/log.h"

#include "goldfish/adb/adb-device.h"
#include "goldfish/avd/avd-info.h"
#include "goldfish/avd/avd-finalize.h"
#include "goldfish/vsock/vsock_port_fwd.h"
#include "goldfish/vsock/vsock_low_level.h"
#include "goldfish/battery/goldfish_battery.h"
#include "goldfish/input/virtio-input-android.h"
#include "goldfish/net/virtio-wifi.h"
#include "goldfish/netsim/netsim-netdev.h"
#include "goldfish/netsim/netsim-chardev.h"
#include "goldfish/grpc/grpc-service-device.h"

 // library and initialize the crashpad crash engine upon launch.
#include "android/crashreport/crash-initializer.h"

#include "google/system/aemu_func_defs.h"

extern "C" void GF_REGISTER_TYPES_FUNC(void) {
    VLOG(1) << "Enter GF_REGISTER_TYPES";
    goldfish_battery_register_types();
    vsock_port_fwd_register_types();
    vsock_low_level_register_types();
    goldfish::avd_info::avd_info_register_types();
    goldfish::avd_finalize::avd_finalize_register_types();
    goldfish::adb_device::adb_device_register_types();
    virtio_input_android_register_types();
    grpc_register_types();
    virtio_wifi_register_types();
    goldfish::netsim::netsim_netdev_register_types();
    goldfish::netsim::netsim_chardev_register_types();
    VLOG(1) << "Exit GF_REGISTER_TYPES";
}

extern "C" void GF_STARTUP_FUNC(int argc, char **argv) {
  absl::InitializeLog();
  absl::log_internal::EnableSymbolizeLogStackTrace(true);
  // What should we log before the AVD module is loaded and configures it properly?
  // TODO Decide what to set this to.
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  if (!crashhandler_init(argc, argv)) {
    LOG(WARNING) << "Failed to initialize crashreporting.";
  }

  LOG(INFO) << "goldfish plugin initialization completed";
}

extern "C" void GF_SHUTDOWN_FUNC(void) {
}
