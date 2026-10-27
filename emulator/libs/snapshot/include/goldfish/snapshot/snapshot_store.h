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

#include <filesystem>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace goldfish::snapshot {

// Snapshot version stored in <avd>/snapshots/<name>/version.txt and checked
// before a snapshot is loaded (by the launcher before passing -loadvm, and by
// VmOperations before a runtime load). Layout of the 32-bit value:
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
// The launcher composes the full version with MakeSnapshotVersion() (it is the
// only goldfish binary that may depend on gfxstream headers) and hands it to
// the QEMU plugin through the avdstart device, so that both sides agree.
//
// Bump kSnapshotVersionBase for incompatible changes on the goldfish side
// (e.g. VMState or hardware-properties.ini layout changes).
// History:
//   1 -> 2: hardware-properties.ini retired cachePartition/useQcow2 fields
//           (b/573413085) and folded gfxstream::kFramebufferSnapshotVersion
//           into bits [30..16].
//   2 -> 3: the renderer (gfxstream/rutabaga) state moved from
//           <avd>/renderersave/default_boot/ to <avd>/snapshots/<name>/renderer/
//           (b/574891578); older snapshots cannot find their renderer state.
inline constexpr int kSnapshotVersionBase = 3;
static_assert(kSnapshotVersionBase > 0 && kSnapshotVersionBase < (1 << 16),
              "Base snapshot version does not fit in 16 bits.");

// The full snapshot version for the given gfxstream framebuffer snapshot
// version, which must fit in 15 bits.
constexpr int MakeSnapshotVersion(int gfxstream_snapshot_version) {
    return (gfxstream_snapshot_version << 16) | kSnapshotVersionBase;
}

// Whether `name` can be used as a snapshot name. A snapshot name is both the
// qcow2 internal snapshot tag and a directory name under <avd>/snapshots, so
// it must be non-empty, must not be ".", and must not contain "..", path
// separators ('/', '\\'), or characters reserved in filenames on Windows
// (':', '*', '?', '"', '<', '>', '|').
bool IsValidSnapshotName(std::string_view name);

// OK if IsValidSnapshotName(name), else INVALID_ARGUMENT with a message that
// spells out the rules, for callers that report the name to a user.
absl::Status ValidateSnapshotName(std::string_view name);

// Owns the layout and lifecycle of the per-snapshot metadata that lives
// outside the qcow2 image, under <avd>/snapshots/<name>/. Snapshot names are
// opaque here: only the launcher knows about the Quickboot snapshot
// ("default_boot", the -snapshot default); QEMU and the plugin treat every
// snapshot alike.
//
//
//   <avd>/snapshots/<name>/version.txt   Version() at save time
//   <avd>/snapshots/<name>/renderer/     gfxstream/rutabaga renderer state,
//                                        written and read by QEMU's
//                                        virtio-gpu-rutabaga device (which is
//                                        given snapshot_directory=Root())
//
// The VM state itself is the qcow2 internal snapshot tagged <name>; this
// class never touches it. All methods take the snapshot name, never a path,
// and reject invalid names (see IsValidSnapshotName). Implementations must be
// safe to call from any thread; callers serialize operations on one name.
class SnapshotStore {
  public:
    virtual ~SnapshotStore() = default;

    // The snapshot version this emulator writes and accepts.
    virtual int Version() const = 0;

    // <avd>/snapshots
    virtual std::filesystem::path Root() const = 0;
    // <avd>/snapshots/<name>
    virtual std::filesystem::path Dir(std::string_view name) const = 0;

    // Whether <avd>/snapshots/<name>/ exists.
    virtual bool Exists(std::string_view name) const = 0;

    // The version recorded in version.txt. NOT_FOUND if the snapshot has no
    // version file, DATA_LOSS if it cannot be parsed.
    virtual absl::StatusOr<int> ReadVersion(std::string_view name) const = 0;

    // Whether version.txt exists and equals Version().
    virtual bool IsCompatible(std::string_view name) const = 0;

    // Creates <avd>/snapshots/<name>/ (and parents) if missing.
    virtual absl::Status Prepare(std::string_view name) = 0;

    // Creates the directory if needed and records Version() in version.txt,
    // overwriting any previous value.
    virtual absl::Status WriteVersion(std::string_view name) = 0;

    // Removes <avd>/snapshots/<name>/ and everything under it. Succeeds if the
    // directory does not exist.
    virtual absl::Status Remove(std::string_view name) = 0;
};

// SnapshotStore backed by the file system of an AVD content directory.
class FileSnapshotStore : public SnapshotStore {
  public:
    // `snapshot_version` is the value written by WriteVersion() and required
    // by IsCompatible(); see MakeSnapshotVersion().
    FileSnapshotStore(std::filesystem::path avd_content_dir, int snapshot_version);

    int Version() const override { return version_; }
    std::filesystem::path Root() const override;
    std::filesystem::path Dir(std::string_view name) const override;
    bool Exists(std::string_view name) const override;
    absl::StatusOr<int> ReadVersion(std::string_view name) const override;
    bool IsCompatible(std::string_view name) const override;
    absl::Status Prepare(std::string_view name) override;
    absl::Status WriteVersion(std::string_view name) override;
    absl::Status Remove(std::string_view name) override;

  private:
    std::filesystem::path VersionFile(std::string_view name) const;

    const std::filesystem::path root_;
    const int version_;
};

}  // namespace goldfish::snapshot
