// Copyright 2026 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include "android/goldfish/ini_file.h"
#include "android/status/status_macros.h"
#include "goldfish/file/file.h"
#include "goldfish/fsimage/qcow2.h"

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kUsageMessage =
        "avdshallowcopy: shallow copy an AVD\n\n"
        "Usage:\n"
        "  avdshallowcopy <AVD_DIRECTORY> <SRC_NAME> <DST_NAME>\n"
        "Example:\n"
        "  avdshallowcopy ~/.android/avd Medium_Phone Medium_Phone.shallow\n"
        "Arguments:\n"
        "  AVD_DIRECTORY - directory where AVDs are located.\n"
        "  SRC_NAME      - the source AVD name to copy from, it must exist.\n"
        "  DST_NAME      - the destination AVD name to copy into, it must not exist.\n"
        "Exit codes: zero - success, non zero - an error.\n\n"
        "The tool creates shallow copies of Android Virtual Devices (AVDs).\n\n"
        "It utilizes the qcow2 file feature to track changes on top of changes\n"
        "(i.e. another qcow2 file). In this case, the backing qcow2 file is located\n"
        "in the SRC_NAME AVD.\n\n"
        "Warning: once a shallow AVD is created, the source AVD cannot change.\n"
        "Note: snapshots are not yet supported.\n";

absl::Status ValidateAvdName(std::string_view name) {
    if (name.empty() || name == "." || name == ".." ||
        name.find_first_of("/\\") != std::string_view::npos ||
        fs::path(name) != fs::path(name).filename()) {
        return absl::InvalidArgumentError(
                absl::StrFormat("Invalid AVD name '%s': must be a simple name", name));
    }
    return absl::OkStatus();
}

absl::Status WritePlaceholderFile(const fs::path& dst_path, const fs::path& src_path) {
    std::ofstream ofs(dst_path, std::ios_base::binary | std::ios_base::trunc);
    if (!ofs.good()) {
        return absl::InternalError(absl::StrFormat(
                "Failed to open placeholder file '%s' for writing", dst_path.string()));
    }

    const std::string content = absl::StrCat("Placeholder: ", src_path.string());
    ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
    ofs.close();
    if (!ofs.good()) {
        return absl::InternalError(
                absl::StrFormat("Failed to write placeholder file '%s'", dst_path.string()));
    }
    return absl::OkStatus();
}

absl::Status CopyIniFile(const fs::path& src_ini, const fs::path& dst_ini,
                         const fs::path& dst_avd_dir, std::string_view dst_name) {
    android::goldfish::IniFile ini(src_ini);
    if (!ini.Read()) {
        return absl::InvalidArgumentError(
                absl::StrFormat("Failed to parse source INI file '%s'", src_ini.string()));
    }
    ini.SetBackingFile(dst_ini);
    ini.SetString("path", dst_avd_dir.string());
    ini.SetString("path.rel", absl::StrCat("avd/", dst_name, ".avd"));
    if (!ini.Write()) {
        return absl::InternalError(
                absl::StrFormat("Failed to write destination INI file '%s'", dst_ini.string()));
    }
    return absl::OkStatus();
}

absl::Status UpdateConfigIniFile(const fs::path& config_ini_path, std::string_view dst_name) {
    android::goldfish::IniFile ini(config_ini_path);
    if (!ini.Read()) {
        return absl::InvalidArgumentError(
                absl::StrFormat("Failed to parse config.ini file '%s'", config_ini_path.string()));
    }
    ini.SetString("AvdId", std::string(dst_name));
    ini.SetString("avd.ini.displayname", std::string(dst_name));
    if (!ini.Write()) {
        return absl::InternalError(
                absl::StrFormat("Failed to write config.ini file '%s'", config_ini_path.string()));
    }
    return absl::OkStatus();
}

absl::Status ShallowCopyDirectoryRecursive(const fs::path& src_dir, const fs::path& dst_dir) {
    std::error_code ec;
    for (fs::directory_iterator it(src_dir, ec); !ec && it != fs::directory_iterator();
         it.increment(ec)) {
        const fs::path src_path = it->path();
        const fs::path dst_path = dst_dir / src_path.filename();

        const fs::file_status st = it->symlink_status(ec);
        if (ec) {
            return absl::InternalError(
                    absl::StrFormat("Failed to stat '%s': %s", src_path.string(), ec.message()));
        }

        if (st.type() == fs::file_type::directory) {
            RETURN_IF_ERROR(android::base::file::mkdir(dst_path, 0755));
            RETURN_IF_ERROR(ShallowCopyDirectoryRecursive(src_path, dst_path));
        } else if (st.type() == fs::file_type::regular) {
            const std::string ext = src_path.extension().string();
            if (absl::EqualsIgnoreCase(ext, ".img")) {
                RETURN_IF_ERROR(WritePlaceholderFile(dst_path, src_path));
            } else if (absl::EqualsIgnoreCase(ext, ".qcow2")) {
                RETURN_IF_ERROR(goldfish::fsimage::CreateQcow2Overlay(dst_path, src_path));
            } else {
                RETURN_IF_ERROR(android::base::file::cp_file(src_path, dst_path));
            }
        } else {
            // symlinks are not supported
            return absl::InvalidArgumentError(absl::StrFormat("Unsupported file type (%d) at '%s'",
                                                              static_cast<int>(st.type()),
                                                              src_path.string()));
        }
    }
    if (ec) {
        return absl::InternalError(absl::StrFormat("Failed to iterate directory '%s': %s",
                                                   src_dir.string(), ec.message()));
    }
    return absl::OkStatus();
}

absl::Status ShallowCopyAvd(const fs::path& avd_dir, std::string_view src_name,
                            std::string_view dst_name) {
    constexpr std::string_view kConfigIni = "config.ini";

    RETURN_IF_ERROR(ValidateAvdName(src_name));
    RETURN_IF_ERROR(ValidateAvdName(dst_name));

    ASSIGN_OR_RETURN(const fs::path abs_avd_dir, android::base::file::make_absolute(avd_dir));

    const fs::path src_ini = abs_avd_dir / absl::StrCat(src_name, ".ini");
    if (!android::base::file::is_file(src_ini)) {
        return absl::NotFoundError(
                absl::StrFormat("Source INI file '%s' does not exist", src_ini.string()));
    }

    const fs::path src_avd_dir = abs_avd_dir / absl::StrCat(src_name, ".avd");
    if (!android::base::file::is_dir(src_avd_dir)) {
        return absl::NotFoundError(
                absl::StrFormat("Source AVD directory '%s' does not exist", src_avd_dir.string()));
    }

    const fs::path src_config_ini = src_avd_dir / kConfigIni;
    if (!android::base::file::is_file(src_config_ini)) {
        return absl::NotFoundError(absl::StrFormat("Source config.ini file '%s' does not exist",
                                                   src_config_ini.string()));
    }

    const fs::path dst_ini = abs_avd_dir / absl::StrCat(dst_name, ".ini");
    if (android::base::file::exists(dst_ini)) {
        return absl::AlreadyExistsError(
                absl::StrFormat("Destination INI file '%s' already exists", dst_ini.string()));
    }

    const fs::path dst_avd_dir = abs_avd_dir / absl::StrCat(dst_name, ".avd");
    if (android::base::file::exists(dst_avd_dir)) {
        return absl::AlreadyExistsError(absl::StrFormat(
                "Destination AVD directory '%s' already exists", dst_avd_dir.string()));
    }

    RETURN_IF_ERROR(android::base::file::mkdir(dst_avd_dir, 0755));
    absl::Cleanup cleanup = [&] {
        android::base::file::rm(dst_ini).IgnoreError();
        android::base::file::rm_recursive(dst_avd_dir).IgnoreError();
    };

    RETURN_IF_ERROR(CopyIniFile(src_ini, dst_ini, dst_avd_dir, dst_name));
    RETURN_IF_ERROR(ShallowCopyDirectoryRecursive(src_avd_dir, dst_avd_dir));
    RETURN_IF_ERROR(UpdateConfigIniFile(dst_avd_dir / kConfigIni, dst_name));
    std::move(cleanup).Cancel();
    return absl::OkStatus();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << kUsageMessage;
        return 1;
    }

    const fs::path avd_dir(argv[1]);
    const std::string_view src_name(argv[2]);
    const std::string_view dst_name(argv[3]);

    const absl::Status status = ShallowCopyAvd(avd_dir, src_name, dst_name);
    if (!status.ok()) {
        std::cerr << "Failed to shallow copy AVD '" << src_name << "' to '" << dst_name
                  << "': " << status << "\n";
        return 1;
    }

    return 0;
}
