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
#include "goldfish/snapshot/snapshot_store.h"
#include "render-utils/snapshot_operations.h"

namespace android::goldfish {

// The snapshot version this emulator writes and accepts; see
// goldfish/snapshot/snapshot_store.h for the layout and history. The launcher
// composes it here (it is the only goldfish binary that depends on gfxstream
// headers) and passes it to the QEMU plugin through the avdstart device.
using ::goldfish::snapshot::kSnapshotVersionBase;

// Name of the Quickboot snapshot, loaded at boot and saved at exit when no
// -snapshot <name> is given. Only the launcher knows this name: QEMU and the
// emulator plugin treat every snapshot alike.
inline constexpr std::string_view kDefaultSnapshotName = "default_boot";

inline constexpr int kGfxstreamSnapshotVersion =
        static_cast<int>(gfxstream::kFramebufferSnapshotVersion);
static_assert(kGfxstreamSnapshotVersion > 0 && kGfxstreamSnapshotVersion < (1 << 15),
              "gfxstream framebuffer snapshot version does not fit in 15 bits.");

inline constexpr int kSnapshotVersion =
        ::goldfish::snapshot::MakeSnapshotVersion(kGfxstreamSnapshotVersion);
static_assert(kSnapshotVersion > 0, "Snapshot version must be a positive int32.");

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
    bool is_enabled() const {
        return load_status_ == LoadStatus::kLoading || save_status_ == SaveStatus::kSaving;
    }

    static std::string_view get_snapshot_name(const EmulatorConfig& emulator);

  private:
    static bool should_load_snapshot(const EmulatorConfig& emulator, std::string& reason,
                                     LoadStatus* status = nullptr);
    static bool should_save_snapshot(const EmulatorConfig& emulator, std::string& reason,
                                     SaveStatus* status = nullptr);

    std::string snapshot_name_;
    bool strict_snapshot_load_ = false;
    LoadStatus load_status_ = LoadStatus::kSkippedOldSnapshot;
    SaveStatus save_status_ = SaveStatus::kDisabledCmdline;
};

}  // namespace android::goldfish
