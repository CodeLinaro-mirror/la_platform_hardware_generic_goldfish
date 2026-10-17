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

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "goldfish/snapshot/snapshot_store.h"

namespace android::goldfish {

using ::goldfish::snapshot::FileSnapshotStore;

absl::Status SnapshotDevice::initialize(const EmulatorConfig& emulator) {
    snapshot_name_ = get_snapshot_name(emulator);
    // A malformed -snapshot name is a user error: report it as such instead of
    // letting it surface later as a confusing "version missing" cold boot.
    if (auto s = ::goldfish::snapshot::ValidateSnapshotName(snapshot_name_); !s.ok()) {
        return s;
    }
    strict_snapshot_load_ = emulator.opts().strict_snapshot_load;

    FileSnapshotStore store(emulator.avd().GetContentPath(), kSnapshotVersion);

    std::string skip_load_reason;
    if (should_load_snapshot(emulator, skip_load_reason, &load_status_)) {
        if (store.IsCompatible(snapshot_name_)) {
            LOG(INFO) << "Loading '" << snapshot_name_ << "' ...";
        } else {
            if (strict_snapshot_load_) {
                return absl::FailedPreconditionError(
                        "Unable to load snapshot: version missing or mismatch");
            }
            LOG(WARNING) << "Snapshot version missing or mismatch, performing cold boot.";
            load_status_ = LoadStatus::kSkippedOldSnapshot;
        }
    } else {
        if (strict_snapshot_load_) {
            return absl::FailedPreconditionError(
                    absl::StrCat("Unable to load snapshot: ", skip_load_reason));
        }
        LOG(WARNING) << "Snapshot load is disabled: " << skip_load_reason
                     << ", performing cold boot.";
    }

    std::string skip_save_reason;
    if (should_save_snapshot(emulator, skip_save_reason, &save_status_)) {
        // Only create the directory here. version.txt is written once a save
        // actually completes (see QemuVmOperations), so that a crash before the
        // exit-time save cannot leave a version file next to stale or missing
        // qcow2 state.
        if (auto s = store.Prepare(snapshot_name_); !s.ok()) {
            LOG(WARNING) << "Failed to create snapshot directory: " << s
                         << ", and snapshot save is disabled.";
            save_status_ = SaveStatus::kDirectoryError;
        }
    } else {
        LOG(WARNING) << "Snapshot save is disabled: " << skip_save_reason << ".";
    }

    return absl::OkStatus();
}

std::vector<std::string> SnapshotDevice::getQemuParameters(
        const EmulatorConfig& /*emulator*/) const {
    std::vector<std::string> params;
    if (load_status_ == LoadStatus::kLoading) {
        params.push_back("-loadvm");
        params.push_back(snapshot_name_);
        if (strict_snapshot_load_) {
            params.push_back("-strict-loadvm");
        }
    }
    if (save_status_ == SaveStatus::kSaving) {
        params.push_back("-savevm");
        params.push_back(snapshot_name_);
    }
    return params;
}

bool SnapshotDevice::should_load_snapshot(const EmulatorConfig& emulator, std::string& reason,
                                          LoadStatus* status) {
    const auto& o = emulator.opts();
    const auto& a = emulator.avd();
    if (a.Hw().fastboot_forceColdBoot) {
        reason = "fastboot.forceColdBoot is true";
        if (status) {
            *status = LoadStatus::kSkippedAvd;
        }
        return false;
    }
    if (o.no_snapshot) {
        reason = "-no-snapshot is set";
        if (status) {
            *status = LoadStatus::kSkippedCmdline;
        }
        return false;
    }
    if (o.no_snapshot_load) {
        reason = "-no-snapshot-load is set";
        if (status) {
            *status = LoadStatus::kSkippedCmdline;
        }
        return false;
    }
    // Hidden alias of -no-snapshot-load, still used by Android Studio for Cold Boot.
    if (o.no_snapstorage) {
        reason = "-no-snapstorage is set";
        if (status) {
            *status = LoadStatus::kSkippedCmdline;
        }
        return false;
    }
    if (status) {
        *status = LoadStatus::kLoading;
    }
    return true;
}

bool SnapshotDevice::should_save_snapshot(const EmulatorConfig& emulator, std::string& reason,
                                          SaveStatus* status) {
    const auto& o = emulator.opts();
    const auto& a = emulator.avd();
    if (a.Hw().fastboot_forceColdBoot) {
        reason = "fastboot.forceColdBoot is true";
        if (status) {
            *status = SaveStatus::kDisabledAvd;
        }
        return false;
    }
    if (o.no_snapshot) {
        reason = "-no-snapshot is set";
        if (status) {
            *status = SaveStatus::kDisabledCmdline;
        }
        return false;
    }
    if (o.no_snapshot_save) {
        reason = "-no-snapshot-save is set";
        if (status) {
            *status = SaveStatus::kDisabledCmdline;
        }
        return false;
    }
    if (status) {
        *status = SaveStatus::kSaving;
    }
    return true;
}

std::string_view SnapshotDevice::get_snapshot_name(const EmulatorConfig& emulator) {
    const auto& o = emulator.opts();
    return (o.snapshot && o.snapshot[0] != '\0') ? o.snapshot : kDefaultSnapshotName;
}

}  // namespace android::goldfish
