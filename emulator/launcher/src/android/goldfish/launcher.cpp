// Copyright (C) 2024 The Android Open Source Project
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
//
#include <filesystem>
#include <string>

#include "absl/log/initialize.h"
#include "absl/log/internal/globals.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_cat.h"

#include "aemu_version.h"
#include "aemu/base/utils/status_macros.h"
#include "android/base/bazel/bazel_info.h"
#include "android/base/system/System.h"
#include "android/cmdline-definitions.h"
#include "android/cmdline-option.h"
#include "android/crashreport/CrashReporter.h"
#include "android/crashreport/crash-initializer.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/config_dirs.h"
#include "android/goldfish/config/emulator.h"
#include "android/goldfish/logging.h"
#include "android/main-help.h"

#include "android/goldfish/config/input_paths.h"

namespace fs = std::filesystem;

using android::base::Bazel;
using android::base::System;
using android::goldfish::Avd;
using android::goldfish::Emulator;

namespace {
static void show_banner() {
    constexpr std::string_view platform = PLATFORM " (" TARGET_CPU "), " COMPILATION_MODE;
    std::cout << "              .: .          \n";
    std::cout << "            .    -            Welcome to goldfish\n";
    std::cout << "        ==:    .-+       =-   The android emulator\n";
    std::cout << "     :+            :  #:  .   Version: " VERSION << "-" << BUILD_ID << "\n";
    std::cout << "    %     @         :@-   -   Platform: " << platform << "\n";
    std::cout << "   :              *=   - -    Copyright 2024 The Android Open Source Project\n";
    std::cout << "   :  -<      :+++      - \n";
    std::cout << "    = _ _.*= .            \n";
}

absl::StatusOr<fs::path> check_exists(fs::path path, std::string_view description) {
    if (!fs::exists(path)) {
        return absl::NotFoundError(absl::StrCat("Path for \"", description, "\" does not exist: ", path.string()));
    }
    VLOG(1) << "Path for \"" << description << "\" exists: " << path.string();
    return path;
}

absl::StatusOr<fs::path> canonicalize(const fs::path &path) {
    std::error_code ec;
    auto canon = fs::canonical(path, ec);
    if (ec) {
        return absl::InternalError(absl::StrCat("Failed to canonicalise path: ", path.string(), " - ", ec.message()));
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
    if (Bazel::inBazel()) {
        constexpr std::string_view kSigned = ".signed";
        absl::StrAppend(&binary, kSigned);
    }
#endif
    return add_binary_suffix(std::move(binary));
}

absl::StatusOr<ResolvedInputPaths> resolve_paths(bool verbose_sdk_search) {
    ResolvedInputPaths paths;
    ASSIGN_OR_RETURN(paths.launcher_binary, check_exists(System::getProgramBinaryPath(), "launcher binary"));

    if (auto d = System::getEnvironmentVariable("ANDROID_EMULATOR_LAUNCHER_DIR"); !d.empty()) {
        paths.launcher_directory = fs::path(d);
        // Sanity check launcher directory
        if (auto launcher = paths.launcher_directory / add_binary_suffix("goldfish"); !fs::exists(launcher)) {
            LOG(WARNING) << "launcher does not appear to exist within overridden launcher directory: " << launcher.string();
        } else if (auto canon = canonicalize(launcher); !canon.ok()) {
            LOG(WARNING) << "unable to canonicalize launcher binary in overridden launcher directory: " << launcher.string();
        } else if (*canon != paths.launcher_binary) {
            LOG(WARNING) << "launcher binary in overridden launcher directory does not seem to match the current binary: " << canon->string() << " vs " << launcher.string();
        }
    } else {
        paths.launcher_directory = paths.launcher_binary.parent_path();
        // Only set this if it wasn't already set as some integrators set it externally.
        System::setEnvironmentVariable("ANDROID_EMULATOR_LAUNCHER_DIR",
                                              System::pathAsString(paths.launcher_directory));
    }
    RETURN_IF_ERROR(check_exists(paths.launcher_directory, "launcher directory").status());

    // TODO Add a debug option to recursively list files in the launcher dir.

    ASSIGN_OR_RETURN(paths.binary_directory, check_exists(paths.launcher_directory / "bin", "binary directory"));
    ASSIGN_OR_RETURN(paths.library_directory, check_exists(paths.launcher_directory / "lib" / "qemu", "library directory"));
    ASSIGN_OR_RETURN(paths.lib64_directory, check_exists(paths.launcher_directory / "lib64", "lib64 directory"));
    // This is used by Qemu aemu_main.c to locate the goldfish plugin library.
    // It is also used by gfxstream to locate the GL and Vulkan libraries.
    System::setEnvironmentVariable("ANDROID_EMULATOR_LIBRARY_DIR", System::pathAsString(paths.library_directory));
    ASSIGN_OR_RETURN(paths.bios_directory, check_exists(paths.launcher_directory / "share" / "qemu", "bios directory"));

    ASSIGN_OR_RETURN(paths.discovery_directory, check_exists(android::goldfish::ConfigDirs::getDiscoveryDirectory(), "discovery directory"));
    ASSIGN_OR_RETURN(paths.sdk_directory, check_exists(android::goldfish::ConfigDirs::getSdkRootDirectory(verbose_sdk_search), "sdk directory"));
    ASSIGN_OR_RETURN(paths.avd_directory, check_exists(android::goldfish::ConfigDirs::getAvdRootDirectory(), "avd directory"));

    ASSIGN_OR_RETURN(paths.qemu_system_x86_binary, check_exists(paths.binary_directory / add_qemu_binary_suffix("qemu-system-x86_64"), "qemu-system-x86_64"));
#ifndef _WIN32
    ASSIGN_OR_RETURN(paths.qemu_system_arm_binary, check_exists(paths.binary_directory / add_qemu_binary_suffix("qemu-system-aarch64"), "qemu-system-aarch64"));
    // ASSIGN_OR_RETURN(paths.qemu_system_riscv_binary, check_exists(paths.binary_directory / add_qemu_binary_suffix("qemu-system-riscv64"), "qemu-system-riscv64"));
#endif
    ASSIGN_OR_RETURN(paths.qemu_img_binary, check_exists(paths.binary_directory / add_binary_suffix("qemu-img"), "qemu-img"));
    // TODO(whollins): ASSIGN_OR_RETURN(paths.netsim_binary, check_exists(paths.binary_directory / add_binary_suffix("netsimd"), "netsimd"));
    ASSIGN_OR_RETURN(paths.crashpad_handler_binary, check_exists(paths.binary_directory / add_binary_suffix("crashpad_handler"), "crashpad handler"));

#ifdef _WIN32
    // Canonicalize binaries as Windows cannot execute a symlink.
    // Note that Forge seems to break if we do this for Linux.
    ASSIGN_OR_RETURN(paths.qemu_system_x86_binary, canonicalize(paths.qemu_system_x86_binary));
    // ASSIGN_OR_RETURN(paths.qemu_system_arm_binary, canonicalize(paths.qemu_system_arm_binary));
    // ASSIGN_OR_RETURN(paths.qemu_system_riscv_binary, canonicalize(paths.qemu_system_riscv_binary));
    ASSIGN_OR_RETURN(paths.qemu_img_binary, canonicalize(paths.qemu_img_binary));
    ASSIGN_OR_RETURN(paths.netsim_binary, canonicalize(paths.netsim_binary));
    ASSIGN_OR_RETURN(paths.crashpad_handler_binary, canonicalize(paths.crashpad_handler_binary));
#endif

    // Make sure the child process is using the same crashpad handler as we are using.
    // Child uses: android::crashreport::CrashReporter::handlerExe() to retrieve this.
    System::setEnvironmentVariable("AEMU_CRASHPAD_HANDLER", paths.crashpad_handler_binary.string());

    return paths;
}

} // namespace

int main(int argc, char** argv) {
    absl::InitializeLog();
    absl::log_internal::EnableSymbolizeLogStackTrace(true);

    for (int nn = 1; nn < argc; nn++) {
        const char* opt = argv[nn];
        int helpStatus = emulator_parseHelpOption(opt);
        if (helpStatus >= 0) {
            return helpStatus;
        }
    }

#ifdef __linux__
    // Bug: 417138854: work around the log spam "bad fde: FDE is really a CIE"
    System::setEnvironmentVariable("LD_PRELOAD","/lib/x86_64-linux-gnu/libgcc_s.so.1");
#endif
    AndroidOptions opts;
    if (android_parse_options(&argc, &argv, &opts) < 0) {
        return 1;
    }

    configureLogging(opts);

    if (opts.list_avds) {
        auto avds = Avd::list();
        for (const auto& name : avds) {
            auto a = Avd::fromName(name, opts.sysdir ? opts.sysdir : "");
            if (!a.status().ok()) {
                std::cout << name << "is not valid: " << a.status().message();
            } else {
                std::cout << (*a)->details(opts.verbose) << '\n';
            }
        }
        return 0;
    }

    Bazel::storeCommandLineArgs(argc, argv);
    show_banner();

    if (Bazel::inBazel()) {
        // We are running in the bazel environment, make sure the plugins and binaries can be found.
        auto launcher_dir = fs::path(
                Bazel::runfilesPath("_main/hardware/generic/goldfish/emulator/launcher"));
        assert(fs::exists(launcher_dir));
        System::setEnvironmentVariable("ANDROID_EMULATOR_LAUNCHER_DIR", System::pathAsString(launcher_dir));
    }

    // Check that things exist so that we can error out early if necessary.
    auto resolved_paths = resolve_paths(opts.verbose);
    if (!resolved_paths.ok()) {
        LOG(ERROR) << "Failed to resolve paths: " << resolved_paths.status();
        return 1;
    }

    if (!crashhandler_init(argc, argv)) {
        LOG(WARNING) << "Failed to initialize crashreporting.";
    }

    // This is needed for gfxstream to be able to load GL libs.
    // TODO: consider moving this to gfxstream itself via the ANDROID_EMULATOR_LIBRARY_DIR env var.
    System::get()->addLibrarySearchDir(resolved_paths->library_directory.string());
    System::get()->addLibrarySearchDir(resolved_paths->lib64_directory.string());

    if (!opts.avd) {
        LOG(ERROR) << "No AVD specified. Use '@foo' or '-avd foo' to launch a virtual device named "
                      "'foo'";
        return 1;
    }

    auto name = opts.avd;

    fs::path sysdir_override;
    if (opts.sysdir) {
        sysdir_override = fs::path(opts.sysdir);
    }

    fs::path writable_content_override;
    if (opts.read_only) {
        writable_content_override = System::get()->getTempDir();
        VLOG(1) << "Content path overridden to: " << writable_content_override;
        fs::create_directories(writable_content_override);
    } else if (opts.datadir) {
        writable_content_override = fs::path(opts.datadir);
        if (!fs::exists(writable_content_override)) {
            LOG(ERROR) << "-datadir specified does not exist: " << writable_content_override;
            return 1;
        }
        if (!fs::is_directory(writable_content_override)) {
            LOG(ERROR) << "-datadir specified is not a directory: " << writable_content_override;
            return 1;
        }
        VLOG(1) << "Content path overridden to: " << writable_content_override;
    }

    auto avd = Avd::fromName(name, sysdir_override, writable_content_override);
    if (!avd.ok()) {
        LOG(ERROR) << "Failed to load " << name << " due to " << avd.status().message();
        return 1;
    }

    Emulator emulator{*std::move(resolved_paths), std::move(avd.value()), opts};

    if (auto s = emulator.launch(); !s.ok()) {
        LOG(FATAL) << "Fatal error whilst launching the emulator: " << s;
    }
    return 0;
}
