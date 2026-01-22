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

#include <memory>
#include <string>

#include "android/cmdline_option.h"
#include "android/goldfish/avd.h"
#include "android/goldfish/input_paths.h"

namespace android::goldfish {

struct EmulatorPorts {
    int serial_number;
    int adb_port;
};

struct ChardevEndpoints {
    std::string netsim;
};

class EmulatorConfig {
  public:
    EmulatorConfig(EmulatorPorts ports, ChardevEndpoints chardev_endpoints,
                   ResolvedInputPaths resolved_paths, std::unique_ptr<Avd> avd, AndroidOptions opts)
            : mPorts(std::move(ports))
            , mChardevEndpoints(std::move(chardev_endpoints))
            , mResolvedPaths(std::move(resolved_paths))
            , mAvd(std::move(avd))
            , mOpts(std::move(opts)) {}

    // The resolved paths to input binaries and data
    const ResolvedInputPaths& paths() const { return mResolvedPaths; }

    // The avd description used to configure this emulator
    const Avd& avd() const { return *mAvd; }

    // The android options used to configure this emulator
    const AndroidOptions& opts() const { return mOpts; }

    const ChardevEndpoints& chardev_endpoints() const { return mChardevEndpoints; }

    int serial_number() const { return mPorts.serial_number; }
    int adb_port() const { return mPorts.adb_port; }

  private:
    const EmulatorPorts mPorts;
    const ChardevEndpoints mChardevEndpoints;

    const ResolvedInputPaths mResolvedPaths;
    const std::unique_ptr<Avd> mAvd;
    const AndroidOptions mOpts;
};

}  // namespace android::goldfish
