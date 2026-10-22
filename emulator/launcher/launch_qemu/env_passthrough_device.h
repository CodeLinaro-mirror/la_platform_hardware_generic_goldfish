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

#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"

#include "device.h"

namespace android::goldfish {

// Checks already-set environment variables against an allowlist and passes them
// through to the QEMU process.
class EnvPassthroughDevice : public Device {
  public:
    EnvPassthroughDevice() : Device("env_passthrough") {}

    absl::Status initialize(const EmulatorConfig& emulator) override { return absl::OkStatus(); }
    absl::flat_hash_map<std::string, std::string> getQemuEnv(
            const EmulatorConfig& emulator) const override;
};

}  // namespace android::goldfish
