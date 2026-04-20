// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS);
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <string>

#include "android/cmdline_option.h"
#include "android/goldfish/avd.h"
#include "android/goldfish/input_paths.h"
#include "goldfish/metrics/configure_metrics_writer.h"

namespace android::goldfish {

struct EmulatorPorts {
    int serial_number = 0;
    int adb_port = 0;
    int qmp_port = 0;
};

struct ChardevEndpoints {
    std::string netsim;
    std::string modem_simulator;
    int modem_simulator_host_id = 0;
};

struct MetricsConfig {
    std::string session_id;
    ::goldfish::metrics::MetricsWriterConfig writer_config;
};

class EmulatorConfig {
  public:
    EmulatorConfig(const EmulatorPorts& ports, const ChardevEndpoints& chardev_endpoints,
                   MetricsConfig metrics_config, const UserPaths& user_paths,
                   const EmulatorPaths& emulator_paths, const Avd& avd, const AndroidOptions& opts)
            : mPorts(ports)
            , mChardevEndpoints(chardev_endpoints)
            , mMetricsConfig(std::move(metrics_config))
            , mUserPaths(user_paths)
            , mEmulatorPaths(emulator_paths)
            , mAvd(avd)
            , mOpts(opts) {}

    const UserPaths& user_paths() const { return mUserPaths; }

    // The resolved paths to input binaries and data
    const EmulatorPaths& emulator_paths() const { return mEmulatorPaths; }

    // The avd description used to configure this emulator
    const Avd& avd() const { return mAvd; }

    // The android options used to configure this emulator
    const AndroidOptions& opts() const { return mOpts; }

    const ChardevEndpoints& chardev_endpoints() const { return mChardevEndpoints; }

    const MetricsConfig& metrics_config() const { return mMetricsConfig; }

    int serial_number() const { return mPorts.serial_number; }
    int adb_port() const { return mPorts.adb_port; }
    int qmp_port() const { return mPorts.qmp_port; }

  private:
    const EmulatorPorts& mPorts;
    const ChardevEndpoints& mChardevEndpoints;
    const MetricsConfig mMetricsConfig;

    const UserPaths& mUserPaths;
    const EmulatorPaths& mEmulatorPaths;
    const Avd& mAvd;
    const AndroidOptions& mOpts;
};

}  // namespace android::goldfish
