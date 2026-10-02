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
#include <string_view>
#include <vector>

#include "device.h"
#include "goldfish/metrics/snapshot_metrics.h"

namespace android::goldfish {

// Handles QEMU snapshot command line parameters
class SnapshotDevice : public Device {
  public:
    using LoadStatus = ::goldfish::metrics::QuickbootLoadStatus;
    using SaveStatus = ::goldfish::metrics::QuickbootSaveStatus;

    explicit SnapshotDevice() : Device("snapshot") {}

    absl::Status initialize(const EmulatorConfig& emulator) override;
    std::vector<std::string> getQemuParameters(const EmulatorConfig& emulator) const override;

    LoadStatus load_status() const { return load_status_; }
    SaveStatus save_status() const { return save_status_; }

    static bool should_load_snapshot(const EmulatorConfig& emulator, std::string& reason,
                                     LoadStatus* status = nullptr);
    static bool should_save_snapshot(const EmulatorConfig& emulator, std::string& reason,
                                     SaveStatus* status = nullptr);
    static std::string_view get_snapshot_name(const EmulatorConfig& emulator);
    static bool snapshotExists(const EmulatorConfig& emulator, const std::string& name);

  private:
    std::string snapshot_name_;
    bool strict_snapshot_load_ = false;
    LoadStatus load_status_ = LoadStatus::kSkippedOldSnapshot;
    SaveStatus save_status_ = SaveStatus::kDisabledCmdline;
};

}  // namespace android::goldfish
