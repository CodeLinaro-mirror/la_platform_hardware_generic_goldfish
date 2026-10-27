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
#include "goldfish/snapshot/snapshot_store.h"

#include <fstream>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"

#include "android/status/status_macros.h"
#include "goldfish/file/file.h"

namespace goldfish::snapshot {
namespace {

constexpr std::string_view kSnapshotsDirName = "snapshots";
constexpr std::string_view kVersionFileName = "version.txt";
// Path separators and characters reserved in filenames on Windows.
constexpr std::string_view kInvalidSnapshotNameChars = "/\\:*?\"<>|";

}  // namespace

bool IsValidSnapshotName(std::string_view name) {
    // "." would name the snapshots root itself; ".." (and anything containing it) a parent.
    return !name.empty() && name != "." &&
           name.find_first_of(kInvalidSnapshotNameChars) == std::string_view::npos &&
           !absl::StrContains(name, "..");
}

absl::Status ValidateSnapshotName(std::string_view name) {
    if (!IsValidSnapshotName(name)) {
        return absl::InvalidArgumentError(absl::StrCat(
                "Invalid snapshot name '", name,
                "': must be non-empty, not '.', and must not contain '..' or any of ",
                kInvalidSnapshotNameChars, "."));
    }
    return absl::OkStatus();
}

FileSnapshotStore::FileSnapshotStore(std::filesystem::path avd_content_dir, int snapshot_version)
        : root_(std::move(avd_content_dir) / kSnapshotsDirName), version_(snapshot_version) {}

std::filesystem::path FileSnapshotStore::Root() const { return root_; }

std::filesystem::path FileSnapshotStore::Dir(std::string_view name) const {
    return root_ / name;
}

std::filesystem::path FileSnapshotStore::VersionFile(std::string_view name) const {
    return Dir(name) / kVersionFileName;
}

bool FileSnapshotStore::Exists(std::string_view name) const {
    return IsValidSnapshotName(name) && android::base::file::is_dir(Dir(name));
}

absl::StatusOr<int> FileSnapshotStore::ReadVersion(std::string_view name) const {
    RETURN_IF_ERROR(ValidateSnapshotName(name));
    const std::filesystem::path version_file = VersionFile(name);
    if (!android::base::file::is_file(version_file)) {
        return absl::NotFoundError(
                absl::StrCat("Snapshot '", name, "' has no version file: ", version_file.string()));
    }
    ASSIGN_OR_RETURN(std::string contents, android::base::file::read_whole_file(version_file));
    int version = 0;
    if (!absl::SimpleAtoi(absl::StripAsciiWhitespace(contents), &version)) {
        return absl::DataLossError(absl::StrCat("Snapshot '", name,
                                                "' has an unparsable version file: ",
                                                version_file.string()));
    }
    return version;
}

bool FileSnapshotStore::IsCompatible(std::string_view name) const {
    const absl::StatusOr<int> version = ReadVersion(name);
    return version.ok() && *version == version_;
}

absl::Status FileSnapshotStore::Prepare(std::string_view name) {
    RETURN_IF_ERROR(ValidateSnapshotName(name));
    return android::base::file::mkdir_recursive(Dir(name), 0755);
}

absl::Status FileSnapshotStore::WriteVersion(std::string_view name) {
    RETURN_IF_ERROR(Prepare(name));
    const std::filesystem::path version_file = VersionFile(name);
    std::ofstream ofs(version_file, std::ios::trunc);
    if (!ofs) {
        return absl::InternalError(
                absl::StrCat("Cannot open snapshot version file for writing: ",
                             version_file.string()));
    }
    ofs << version_;
    ofs.close();
    if (!ofs) {
        return absl::InternalError(
                absl::StrCat("Cannot write snapshot version file: ", version_file.string()));
    }
    return absl::OkStatus();
}

absl::Status FileSnapshotStore::Remove(std::string_view name) {
    RETURN_IF_ERROR(ValidateSnapshotName(name));
    const std::filesystem::path dir = Dir(name);
    if (!android::base::file::exists(dir)) {
        return absl::OkStatus();
    }
    return android::base::file::rm_recursive(dir);
}

}  // namespace goldfish::snapshot
