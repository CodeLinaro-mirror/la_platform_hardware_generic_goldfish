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

#include "snapshot_device.h"

#include <filesystem>
#include <fstream>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"

namespace android::goldfish {

constexpr int kSnapshotVersion = 1;

absl::Status SnapshotDevice::initialize(const EmulatorConfig& emulator) {
    return absl::OkStatus();
}

std::vector<std::string> SnapshotDevice::getQemuParameters(const EmulatorConfig& emulator) const {
    std::vector<std::string> params;

    const char* snapshot_name = get_snapshot_name(emulator);
    const std::filesystem::path snapshot_dir =
            emulator.avd().GetContentPath() / "snapshots" / snapshot_name;
    const std::filesystem::path version_file = snapshot_dir / "version.txt";

    std::string skip_load_reason;
    if (should_load_snapshot(emulator, skip_load_reason)) {
        bool version_matches = false;
        if (std::filesystem::exists(version_file)) {
            std::ifstream ifs(version_file);
            int version = 0;
            if (ifs >> version && version == kSnapshotVersion) {
                version_matches = true;
            }
        }

        if (version_matches) {
            LOG(INFO) << "Loading '" << snapshot_name << "' ...";
            params.push_back("-loadvm");
            params.push_back(snapshot_name);
        } else {
            LOG(WARNING) << "Snapshot version missing or mismatch, performing cold boot.";
        }
    } else {
        LOG(WARNING) << "Snapshot load is disabled: " << skip_load_reason
                     << ", performing cold boot.";
    }

    std::string skip_save_reason;
    if (should_save_snapshot(emulator, skip_save_reason)) {
        std::error_code ec;
        std::filesystem::create_directories(snapshot_dir, ec);
        if (!ec) {
            std::ofstream ofs(version_file);
            ofs << kSnapshotVersion;
            params.push_back("-savevm");
            params.push_back(snapshot_name);
        } else {
            LOG(WARNING) << "Failed to create snapshot directory: " << ec.message()
                         << ", and snapshot save is disabled.";
        }
    } else {
        LOG(WARNING) << "Snapshot save is disabled: " << skip_save_reason << ".";
    }

    return params;
}

bool SnapshotDevice::should_load_snapshot(const EmulatorConfig& emulator, std::string& reason) {
    const auto& o = emulator.opts();
    const auto& a = emulator.avd();
    if (a.Hw().fastboot_forceColdBoot) {
        reason = "fastboot.forceColdBoot is true";
        return false;
    }
    if (o.no_snapshot) {
        reason = "-no-snapshot is set";
        return false;
    }
    if (o.no_snapshot_load) {
        reason = "-no-snapshot-load is set";
        return false;
    }
    return true;
}

bool SnapshotDevice::should_save_snapshot(const EmulatorConfig& emulator, std::string& reason) {
    const auto& o = emulator.opts();
    const auto& a = emulator.avd();
    if (a.Hw().fastboot_forceColdBoot) {
        reason = "fastboot.forceColdBoot is true";
        return false;
    }
    if (o.no_snapshot) {
        reason = "-no-snapshot is set";
        return false;
    }
    if (o.no_snapshot_save) {
        reason = "-no-snapshot-save is set";
        return false;
    }
    return true;
}

const char* SnapshotDevice::get_snapshot_name(const EmulatorConfig& emulator) {
    const auto& o = emulator.opts();
    return (o.snapshot && o.snapshot[0] != '\0') ? o.snapshot : "default_boot";
}

}  // namespace android::goldfish
