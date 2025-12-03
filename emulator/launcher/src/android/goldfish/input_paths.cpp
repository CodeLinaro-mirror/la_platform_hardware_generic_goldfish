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

#include "aemu/base/utils/status_macros.h"
#include "android/base/bazel/bazel_info.h"
#include "android/base/system/System.h"
#include "android/goldfish/config/config_dirs.h"
#include "goldfish/async/uv_to_absl.h"
#include "uv.h"

#ifdef _WIN32
#include <windows.h>
#define PATH_MAX MAX_PATH
#else
#include <limits.h>
#endif

namespace android::goldfish {
namespace {

using android::base::System;

absl::StatusOr<fs::path> get_program_path() {
    char buf[PATH_MAX];
    size_t size = sizeof(buf);
    if (int res = uv_exepath(buf, &size); res < 0) {
        return ::goldfish::async::UvErrToAbslStatus(res);
    }
    return fs::path(std::string_view(buf, size));
}

absl::StatusOr<fs::path> check_exists(fs::path path, std::string_view description) {
    if (!fs::exists(path)) {
        return absl::NotFoundError(
                absl::StrCat("Path for \"", description, "\" does not exist: ", path.string()));
    }
    VLOG(1) << "Path for \"" << description << "\" exists: " << path.string();
    return path;
}

absl::StatusOr<fs::path> canonicalize(const fs::path& path) {
    std::error_code ec;
    auto canon = fs::canonical(path, ec);
    if (ec) {
        return absl::InternalError(
                absl::StrCat("Failed to canonicalize path: ", path.string(), " - ", ec.message()));
    }
    if (canon != path) {
        VLOG(1) << "binary is a symlink, replacing with real path: " << path << " -> " << canon;
    }
    return fs::path(canon);
}

std::string add_binary_suffix(std::string binary) {
#ifdef _WIN32
    constexpr std::string_view kExe = ".exe";
    absl::StrAppend(&binary, kExe);
#endif
    return binary;
}

std::string add_qemu_binary_suffix(std::string binary) {
    // Note that this behaviour is currently defined here:
    // https://source.corp.google.com/h/googleplex-android/platform/superproject/main-emu-next-dev/+/main-emu-next-dev:external/qemu/platform/cc_interface_binary.bzl;l=101;drc=9e3171a3998e1fefddb5a024b0e0b5ffcc3f5576
#ifdef __APPLE__
    if (android::base::Bazel::inBazel()) {
        constexpr std::string_view kSigned = ".signed";
        absl::StrAppend(&binary, kSigned);
    }
#endif
    return add_binary_suffix(std::move(binary));
}

}  // namespace

absl::StatusOr<ResolvedInputPaths> resolve_paths(bool verbose_sdk_search) {
    ResolvedInputPaths paths;
    ASSIGN_OR_RETURN(fs::path program_path, get_program_path());
    ASSIGN_OR_RETURN(paths.launcher_binary, check_exists(program_path, "launcher binary"));

    if (auto d = System::getEnvironmentVariable("ANDROID_EMULATOR_LAUNCHER_DIR"); !d.empty()) {
        paths.launcher_directory = fs::path(d);
        // Sanity check launcher directory
        if (auto launcher = paths.launcher_directory / add_binary_suffix("goldfish");
            !fs::exists(launcher)) {
            LOG(WARNING)
                    << "launcher does not appear to exist within overridden launcher directory: "
                    << launcher.string();
        } else if (auto canon = canonicalize(launcher); !canon.ok()) {
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
        System::setEnvironmentVariable("ANDROID_EMULATOR_LAUNCHER_DIR",
                                       paths.launcher_directory.string());
    }
    RETURN_IF_ERROR(check_exists(paths.launcher_directory, "launcher directory").status());

    // TODO Add a debug option to recursively list files in the launcher dir.

    ASSIGN_OR_RETURN(paths.binary_directory,
                     check_exists(paths.launcher_directory / "bin", "binary directory"));
    ASSIGN_OR_RETURN(paths.library_directory,
                     check_exists(paths.launcher_directory / "lib" / "qemu", "library directory"));
    ASSIGN_OR_RETURN(paths.lib64_directory,
                     check_exists(paths.launcher_directory / "lib64", "lib64 directory"));
    // This is used by Qemu aemu_main.c to locate the goldfish plugin library.
    // It is also used by gfxstream to locate the GL and Vulkan libraries.
    System::setEnvironmentVariable("ANDROID_EMULATOR_LIBRARY_DIR",
                                   paths.library_directory.string());
    ASSIGN_OR_RETURN(paths.bios_directory,
                     check_exists(paths.launcher_directory / "share" / "qemu", "bios directory"));

    ASSIGN_OR_RETURN(
            paths.user_directory,
            check_exists(android::goldfish::ConfigDirs::getUserDirectory(), "user directory"));
    ASSIGN_OR_RETURN(
            paths.avd_directory,
            check_exists(android::goldfish::ConfigDirs::getAvdRootDirectory(), "avd directory"));
    ASSIGN_OR_RETURN(paths.sdk_directory,
                     check_exists(android::goldfish::ConfigDirs::getSdkRootDirectory(
                                          paths.launcher_directory, verbose_sdk_search),
                                  "sdk directory"));
    ASSIGN_OR_RETURN(paths.discovery_directory,
                     check_exists(android::goldfish::ConfigDirs::getDiscoveryDirectory(),
                                  "discovery directory"));

    ASSIGN_OR_RETURN(
            paths.qemu_system_x86_binary,
            check_exists(paths.binary_directory / add_qemu_binary_suffix("qemu-system-x86_64"),
                         "qemu-system-x86_64"));
#ifndef _WIN32
    ASSIGN_OR_RETURN(
            paths.qemu_system_arm_binary,
            check_exists(paths.binary_directory / add_qemu_binary_suffix("qemu-system-aarch64"),
                         "qemu-system-aarch64"));
    // ASSIGN_OR_RETURN(paths.qemu_system_riscv_binary, check_exists(paths.binary_directory /
    // add_qemu_binary_suffix("qemu-system-riscv64"), "qemu-system-riscv64"));
#endif
    ASSIGN_OR_RETURN(
            paths.qemu_img_binary,
            check_exists(paths.binary_directory / add_binary_suffix("qemu-img"), "qemu-img"));
    ASSIGN_OR_RETURN(
            paths.netsim_binary,
            check_exists(paths.binary_directory / add_binary_suffix("netsimd"), "netsimd"));
    ASSIGN_OR_RETURN(paths.crashpad_handler_binary,
                     check_exists(paths.binary_directory / add_binary_suffix("crashpad_handler"),
                                  "crashpad handler"));

#ifdef _WIN32
    // Canonicalize binaries as Windows cannot execute a symlink.
    // Note that Forge seems to break if we do this for Linux.
    ASSIGN_OR_RETURN(paths.qemu_system_x86_binary, canonicalize(paths.qemu_system_x86_binary));
    // ASSIGN_OR_RETURN(paths.qemu_system_arm_binary, canonicalize(paths.qemu_system_arm_binary));
    // ASSIGN_OR_RETURN(paths.qemu_system_riscv_binary,
    // canonicalize(paths.qemu_system_riscv_binary));
    ASSIGN_OR_RETURN(paths.qemu_img_binary, canonicalize(paths.qemu_img_binary));
    ASSIGN_OR_RETURN(paths.netsim_binary, canonicalize(paths.netsim_binary));
    ASSIGN_OR_RETURN(paths.crashpad_handler_binary, canonicalize(paths.crashpad_handler_binary));
#endif

    // Make sure the child process is using the same crashpad handler as we are using.
    // Child uses: android::crashreport::CrashReporter::handlerExe() to retrieve this.
    System::setEnvironmentVariable("AEMU_CRASHPAD_HANDLER", paths.crashpad_handler_binary.string());

    return paths;
}

}  // namespace android::goldfish
