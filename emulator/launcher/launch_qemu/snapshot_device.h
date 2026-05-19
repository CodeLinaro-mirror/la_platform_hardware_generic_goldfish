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
#include <vector>

#include "device.h"

namespace android::goldfish {

// Handles QEMU snapshot command line parameters
class SnapshotDevice : public Device {
  public:
    explicit SnapshotDevice() : Device("snapshot") {}

    absl::Status initialize(const EmulatorConfig& emulator) override;
    std::vector<std::string> getQemuParameters(const EmulatorConfig& emulator) const override;

    static bool should_load_snapshot(const EmulatorConfig& emulator, std::string& reason);
    static bool should_save_snapshot(const EmulatorConfig& emulator, std::string& reason);
    static const char* get_snapshot_name(const EmulatorConfig& emulator);
    static bool snapshotExists(const EmulatorConfig& emulator, const std::string& name);
};

}  // namespace android::goldfish
