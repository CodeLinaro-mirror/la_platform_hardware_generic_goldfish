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

#include "android/status/status_macros.h"
#include "android/base/bazel_info.h"
#include "goldfish/file/file.h"
#include "android/base/system.h"
#include "android/goldfish/config_dirs.h"
#include "goldfish/async/uv_to_absl.h"
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

absl::StatusOr<ResolvedInputPaths> ResolvePaths(bool verbose, bool include_fishtank) {
    ResolvedInputPaths paths;
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

    ASSIGN_OR_RETURN(
            paths.user_directory,
            CheckExists(android::goldfish::ConfigDirs::GetUserDirectory(), "user directory"));
    ASSIGN_OR_RETURN(
            paths.avd_directory,
            CheckExists(android::goldfish::ConfigDirs::GetAvdRootDirectory(), "avd directory"));
    ASSIGN_OR_RETURN(paths.sdk_directory,
                     CheckExists(android::goldfish::ConfigDirs::GetSdkRootDirectory(
                                         paths.launcher_directory, verbose),
                                 "sdk directory"));
    ASSIGN_OR_RETURN(paths.discovery_directory,
                     CheckExists(android::goldfish::ConfigDirs::GetDiscoveryDirectory(),
                                 "discovery directory"));

    ASSIGN_OR_RETURN(paths.qemu_system_x86_binary,
                     CheckExists(paths.binary_directory / AddBinarySuffix("qemu-system-x86_64"),
                                 "qemu-system-x86_64"));
#ifndef _WIN32
    ASSIGN_OR_RETURN(
            paths.qemu_system_arm_binary,
            CheckExists(paths.binary_directory / AddBinarySuffix("qemu-system-aarch64"),
                        "qemu-system-aarch64"));
    // ASSIGN_OR_RETURN(paths.qemu_system_riscv_binary, check_exists(paths.binary_directory /
    // add_qemu_binary_suffix("qemu-system-riscv64"), "qemu-system-riscv64"));
#endif
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
    // ASSIGN_OR_RETURN(paths.qemu_system_arm_binary, canonicalize(paths.qemu_system_arm_binary));
    // ASSIGN_OR_RETURN(paths.qemu_system_riscv_binary,
    // canonicalize(paths.qemu_system_riscv_binary));
    ASSIGN_OR_RETURN(paths.qemu_img_binary, Canonicalize(paths.qemu_img_binary));
    ASSIGN_OR_RETURN(paths.netsim_binary, Canonicalize(paths.netsim_binary));
    ASSIGN_OR_RETURN(paths.crashpad_handler_binary, Canonicalize(paths.crashpad_handler_binary));
#endif

    // Make sure the child process is using the same crashpad handler as we are using.
    // Child uses: android::crashreport::CrashReporter::handlerExe() to retrieve this.
    System::SetEnvironmentVariable("AEMU_CRASHPAD_HANDLER", paths.crashpad_handler_binary.string());

    return paths;
}

}  // namespace android::goldfish
