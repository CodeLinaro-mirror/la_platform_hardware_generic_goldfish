// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS);
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "android/goldfish/input_paths.h"

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

#include "android/base/system.h"
#include "android/goldfish/config_dirs.h"
#include "android/status/status_macros.h"
#include "goldfish/async/uv_to_absl.h"
#include "goldfish/discovery/emulator_advertisement.h"
#include "goldfish/file/file.h"
#include "uv.h"

#ifdef _WIN32
#include <windows.h>
#define PATH_MAX MAX_PATH
#else
#include <climits>
#endif

namespace android::goldfish {
namespace {

using android::base::System;

absl::StatusOr<fs::path> GetProgramPath() {
    char buf[PATH_MAX];
    size_t size = sizeof(buf);
    if (const int res = uv_exepath(buf, &size); res < 0) {
        return ::goldfish::async::UvErrToAbslStatus(res);
    }
    return fs::path(std::string_view(buf, size));
}

absl::StatusOr<fs::path> CheckExists(fs::path path, std::string_view description) {
    if (!android::base::file::exists(path)) {
        return absl::NotFoundError(
                absl::StrCat("Path for \"", description, "\" does not exist: ", path.string()));
    }
    if (!android::base::file::can_read(path)) {
        return absl::PermissionDeniedError(
                absl::StrCat("No read permissions for \"", description, "\": ", path.string()));
    }
    VLOG(1) << "Path for \"" << description << "\" exists: " << path.string();
    return path;
}

absl::StatusOr<fs::path> Canonicalize(const fs::path& path) {
    ASSIGN_OR_RETURN(auto canon, android::base::file::make_canonical(path));
    if (canon != path) {
        VLOG(1) << "binary is a symlink, replacing with real path: " << path << " -> " << canon;
    }
    return canon;
}

std::string AddBinarySuffix(std::string binary) {
#ifdef _WIN32
    constexpr std::string_view kExe = ".exe";
    absl::StrAppend(&binary, kExe);
#endif
    return binary;
}

constexpr std::string_view kEmulatorBinaryName = "emulator";

}  // namespace

absl::StatusOr<EmulatorPaths> ResolveEmulatorPaths(bool verbose, bool include_fishtank) {
    EmulatorPaths paths;
    ASSIGN_OR_RETURN(const fs::path program_path, GetProgramPath());
    ASSIGN_OR_RETURN(paths.launcher_binary, CheckExists(program_path, "launcher binary"));

    if (auto d = System::GetEnvironmentVariable("ANDROID_EMULATOR_LAUNCHER_DIR"); !d.empty()) {
        paths.launcher_directory = fs::path(d);
        // Sanity check launcher directory
        if (auto launcher =
                    paths.launcher_directory / AddBinarySuffix(std::string(kEmulatorBinaryName));
            !android::base::file::exists(launcher)) {
            LOG(WARNING)
                    << "launcher does not appear to exist within overridden launcher directory: "
                    << launcher.string();
        } else if (auto canon = Canonicalize(launcher); !canon.ok()) {
            LOG(WARNING)
                    << "unable to canonicalize launcher binary in overridden launcher directory: "
                    << launcher.string();
        } else if (*canon != paths.launcher_binary) {
            LOG(WARNING) << "launcher binary in overridden launcher directory does not seem to "
                            "match the current binary: "
                         << canon->string() << " vs " << launcher.string();
        }
    } else {
        paths.launcher_directory = paths.launcher_binary.parent_path();
        // Only set this if it wasn't already set as some integrators set it externally.
        System::SetEnvironmentVariable("ANDROID_EMULATOR_LAUNCHER_DIR",
                                       paths.launcher_directory.string());
    }
    RETURN_IF_ERROR(CheckExists(paths.launcher_directory, "launcher directory").status());

    if (verbose) {
        LOG(INFO) << "Listing launcher directory (" << paths.launcher_directory << "):";
        for (const auto& path : base::file::scan_dir_recursive(paths.launcher_directory)) {
            LOG(INFO) << "    " << path.lexically_relative(paths.launcher_directory).string();
        }
    }

    ASSIGN_OR_RETURN(paths.binary_directory,
                     CheckExists(paths.launcher_directory / "bin", "binary directory"));
    ASSIGN_OR_RETURN(paths.library_directory,
                     CheckExists(paths.launcher_directory / "lib" / "qemu", "library directory"));
    ASSIGN_OR_RETURN(paths.lib64_directory,
                     CheckExists(paths.launcher_directory / "lib64", "lib64 directory"));
    // This is used by Qemu aemu_main.c to locate the goldfish plugin library.
    // It is also used by gfxstream to locate the GL and Vulkan libraries.
    System::SetEnvironmentVariable("ANDROID_EMULATOR_LIBRARY_DIR",
                                   paths.library_directory.string());
    ASSIGN_OR_RETURN(paths.bios_directory,
                     CheckExists(paths.launcher_directory / "share" / "qemu", "bios directory"));

    ASSIGN_OR_RETURN(paths.qemu_system_x86_binary,
                     CheckExists(paths.binary_directory / AddBinarySuffix("qemu-system-x86_64"),
                                 "qemu-system-x86_64"));
    ASSIGN_OR_RETURN(paths.qemu_system_arm_binary,
                     CheckExists(paths.binary_directory / AddBinarySuffix("qemu-system-aarch64"),
                                 "qemu-system-aarch64"));
    // ASSIGN_OR_RETURN(paths.qemu_system_riscv_binary, check_exists(paths.binary_directory /
    // add_qemu_binary_suffix("qemu-system-riscv64"), "qemu-system-riscv64"));

    ASSIGN_OR_RETURN(paths.qemu_img_binary,
                     CheckExists(paths.binary_directory / AddBinarySuffix("qemu-img"), "qemu-img"));
    ASSIGN_OR_RETURN(paths.netsim_binary,
                     CheckExists(paths.binary_directory / AddBinarySuffix("netsimd"), "netsimd"));
    ASSIGN_OR_RETURN(paths.crashpad_handler_binary,
                     CheckExists(paths.binary_directory / AddBinarySuffix("crashpad_handler"),
                                 "crashpad handler"));
    if (include_fishtank) {
        ASSIGN_OR_RETURN(paths.fishtank_binary, CheckExists(paths.launcher_directory / "fishtank" /
                                                                    AddBinarySuffix("fishtank"),
                                                            "fishtank"));
    }

#ifdef _WIN32
    // Canonicalize binaries as Windows cannot execute a symlink.
    // Note that Forge seems to break if we do this for Linux.
    ASSIGN_OR_RETURN(paths.qemu_system_x86_binary, Canonicalize(paths.qemu_system_x86_binary));
    ASSIGN_OR_RETURN(paths.qemu_system_arm_binary, Canonicalize(paths.qemu_system_arm_binary));
    // ASSIGN_OR_RETURN(paths.qemu_system_riscv_binary, Canonicalize(paths.qemu_system_riscv_binary));
    ASSIGN_OR_RETURN(paths.qemu_img_binary, Canonicalize(paths.qemu_img_binary));
    ASSIGN_OR_RETURN(paths.netsim_binary, Canonicalize(paths.netsim_binary));
    ASSIGN_OR_RETURN(paths.crashpad_handler_binary, Canonicalize(paths.crashpad_handler_binary));
#endif

    // Make sure the child process is using the same crashpad handler as we are using.
    // Child uses: android::crashreport::CrashReporter::handlerExe() to retrieve this.
    System::SetEnvironmentVariable("AEMU_CRASHPAD_HANDLER", paths.crashpad_handler_binary.string());

    return paths;
}

absl::StatusOr<UserPaths> ResolveUserPaths(const fs::path& launcher_dir, bool verbose) {
    UserPaths paths;
    ASSIGN_OR_RETURN(
            paths.user_directory,
            CheckExists(android::goldfish::ConfigDirs::GetUserDirectory(), "user directory"));
    ASSIGN_OR_RETURN(
            paths.avd_directory,
            CheckExists(android::goldfish::ConfigDirs::GetAvdRootDirectory(), "avd directory"));
    ASSIGN_OR_RETURN(
            paths.sdk_directory,
            CheckExists(android::goldfish::ConfigDirs::GetSdkRootDirectory(launcher_dir, verbose),
                        "sdk directory"));
    ASSIGN_OR_RETURN(
            paths.discovery_directory,
            CheckExists(::goldfish::discovery::EmulatorAdvertisement::GetDiscoveryDirectory(),
                        "discovery directory"));

    return paths;
}

namespace {
absl::StatusOr<fs::path> Search(const std::vector<fs::path>& search_paths,
                                std::string_view filename, std::string_view description) {
    VLOG(1) << "Searching for system image file: " << filename;
    for (const auto& sys_path : search_paths) {
        if (auto path = CheckExists(sys_path / filename, description); path.ok()) {
            return path;
        } else {
            VLOG(1) << "Not found in system dir: " << path.status();
        }
    }
    return absl::NotFoundError(absl::StrCat("System image file not found: ", filename));
}
}  // namespace

absl::StatusOr<SystemImagePaths> ResolveSystemImagePaths(const std::vector<fs::path>& search_paths,
                                                         const AndroidOptions& opts) {
    if (opts.verbose) {
        for (const auto& sdk_path : search_paths) {
            LOG(INFO) << "Listing system image search directory (" << sdk_path << "):";
            for (const auto& path : base::file::scan_dir_recursive(sdk_path)) {
                LOG(INFO) << "    " << path.lexically_relative(sdk_path).string();
            }
        }
    }
    SystemImagePaths paths;
    ASSIGN_OR_RETURN(paths.build_properties,
                     Search(search_paths, "build.prop", "build properties"));
    ASSIGN_OR_RETURN(paths.advanced_features,
                     Search(search_paths, "advancedFeatures.ini", "advanced features"));
    ASSIGN_OR_RETURN(
            paths.verified_boot_params,
            Search(search_paths, "VerifiedBootParams.textproto", "verified boot parameters"));

    ASSIGN_OR_RETURN(paths.data_dir, Search(search_paths, "data", "data directory"));
    if (!base::file::is_dir(paths.data_dir)) {
        return absl::InvalidArgumentError(
                absl::StrCat("data directory is not a directory: ", paths.data_dir.string()));
    }

    ASSIGN_OR_RETURN(paths.kernel_cmdline,
                     Search(search_paths, "kernel_cmdline.txt", "kernel cmdline"));

    if (opts.kernel) {
        ASSIGN_OR_RETURN(paths.kernel_image, CheckExists(opts.kernel, "override kernel image"));
    } else {
        ASSIGN_OR_RETURN(paths.kernel_image, Search(search_paths, "kernel-ranchu", "kernel image"));
    }

    if (opts.ramdisk) {
        ASSIGN_OR_RETURN(paths.ramdisk_image, CheckExists(opts.ramdisk, "override ramdisk image"));
    } else {
        ASSIGN_OR_RETURN(paths.ramdisk_image, Search(search_paths, "ramdisk.img", "ramdisk image"));
    }

    if (opts.system) {
        ASSIGN_OR_RETURN(paths.system_image, CheckExists(opts.system, "override system image"));
    } else {
        ASSIGN_OR_RETURN(paths.system_image, Search(search_paths, "system.img", "system image"));
    }
    if (opts.vendor) {
        ASSIGN_OR_RETURN(paths.vendor_image, CheckExists(opts.vendor, "override vendor image"));
    } else {
        ASSIGN_OR_RETURN(paths.vendor_image, Search(search_paths, "vendor.img", "vendor image"));
    }
    if (opts.encryption_key) {
        ASSIGN_OR_RETURN(paths.encryption_key_image,
                         CheckExists(opts.encryption_key, "override encryption key image"));
    } else {
        ASSIGN_OR_RETURN(paths.encryption_key_image,
                         Search(search_paths, "encryptionkey.img", "encryption key image"));
    }

    return paths;
}

}  // namespace android::goldfish
