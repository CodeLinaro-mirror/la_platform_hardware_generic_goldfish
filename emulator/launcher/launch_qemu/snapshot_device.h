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
#include "render-utils/snapshot_operations.h"

namespace android::goldfish {

// Snapshot version stored in <snapshot_dir>/version.txt and checked in
// SnapshotDevice::initialize() before launching QEMU (to decide whether to
// pass -loadvm). Layout of the 32-bit value:
//
//   [30..16]  gfxstream framebuffer snapshot version
//             (gfxstream::kFramebufferSnapshotVersion, 15 bits; bit 31 is
//             left clear so the value is a positive int32)
//   [15.. 0]  kSnapshotVersionBase, the emulator's own format version (16 bits)
//
// Folding the gfxstream version in here means that whenever gfxstream changes
// its framebuffer stream format, snapshots written with the previous format
// are rejected up front and the emulator falls back to a cold boot instead of
// failing inside FrameBuffer::onLoad() after guest RAM and device state have
// already been restored.
//
// Bump kSnapshotVersionBase for incompatible changes on the goldfish side
// (e.g. VMState or hardware-properties.ini layout changes).
// History:
//   1 -> 2: hardware-properties.ini retired cachePartition/useQcow2 fields
//           (b/573413085) and folded gfxstream::kFramebufferSnapshotVersion
//           into bits [30..16].
inline constexpr int kSnapshotVersionBase = 2;
static_assert(kSnapshotVersionBase > 0 && kSnapshotVersionBase < (1 << 16),
              "Base snapshot version does not fit in 16 bits.");

inline constexpr int kGfxstreamSnapshotVersion =
        static_cast<int>(gfxstream::kFramebufferSnapshotVersion);
static_assert(kGfxstreamSnapshotVersion > 0 && kGfxstreamSnapshotVersion < (1 << 15),
              "gfxstream framebuffer snapshot version does not fit in 15 bits.");

inline constexpr int kSnapshotVersion =
        (kGfxstreamSnapshotVersion << 16) | kSnapshotVersionBase;
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
