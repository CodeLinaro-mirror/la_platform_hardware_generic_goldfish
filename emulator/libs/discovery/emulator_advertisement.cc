// Copyright (C) 2020 The Android Open Source Project
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

#include "goldfish/discovery/emulator_advertisement.h"

#include <sys/stat.h>

#include <cstdio>
#include <filesystem>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"

#include "android/base/system.h"
#include "android/goldfish/ini_file.h"
#include "android/process/process.h"
#include "goldfish/file/file.h"
#include "goldfish/file/file_atomic.h"

namespace goldfish::discovery {

namespace fs = std::filesystem;
namespace base = android::base;

using android::base::Process;
using android::goldfish::IniFile;
namespace file = android::base::file;

namespace {

using android::base::System;

static const std::string_view kAndroidSubDir = ".android";

using discovery_dir = struct DiscoveryDir {
    const char* root_env;
    const char* subdir;
};

#if defined(_WIN32)
discovery_dir discovery{"LOCALAPPDATA", "Temp"};
#elif defined(__linux__)
discovery_dir discovery{"XDG_RUNTIME_DIR", ""};
#elif defined(__APPLE__)
discovery_dir discovery{"HOME", "Library/Caches/TemporaryItems"};
#else
#error This platform is not supported.
#endif

fs::path GetUserDirectory() {
    fs::path home = System::Get()->EnvGet("ANDROID_EMULATOR_HOME");
    if (!home.empty()) {
        return home;
    }

    home = System::Get()->EnvGet("ANDROID_PREFS_ROOT");
    if (!home.empty()) {
        auto home_new_way = fs::path(home) / kAndroidSubDir;
        return file::is_dir(home_new_way) ? home_new_way : home;
    }
    home = System::Get()->EnvGet("ANDROID_SDK_HOME");
    if (!home.empty()) {
        auto home_old_way = fs::path(home) / kAndroidSubDir;
        return file::exists(home_old_way) ? home_old_way : home;
    }

    home = android::base::System::Get()->GetHomeDirectory();
    if (home.empty()) {
        return fs::temp_directory_path();
    }
    return home / kAndroidSubDir;
}

fs::path GetAlternativeRoot() {
#ifdef __linux__
    auto uid = getuid();
    auto discovery_path = fs::path("/run/user/") / std::to_string(uid);
    if (file::exists(discovery_path)) {
        return discovery_path;
    }
#endif

    return GetUserDirectory();
}

}  // namespace

fs::path EmulatorAdvertisement::GetDiscoveryDirectory() {
    fs::path root = System::Get()->EnvGet(discovery.root_env);
    if (root.empty()) {
        LOG(WARNING) << "Using fallback path for the emulator registration directory.";
        root = GetAlternativeRoot();
    } else {
        root = root / discovery.subdir;
    }
    const std::error_code ec;

    auto desired_directory = root / "avd" / "running";
    if (!file::exists(desired_directory)) {
        if (auto s = file::mkdir_recursive(desired_directory, 0700); !s.ok()) {
            LOG(WARNING) << "Unable to create directories: " << desired_directory << " due to "
                         << s;
        }
    } else {
        file::chmod(desired_directory, 0700).IgnoreError();
    }
    return desired_directory;
}

constexpr const char* kLocationFormat = "pid_%d.ini";

bool IsEmulatorProcess(const std::string& exe_path) {
    static const absl::flat_hash_set<std::string_view> kEmulatorBinaries = {
        "emulator",
        "qemu-system-x86_64",
        "qemu-system-aarch64",
        "qemu-system-riscv64",
    };

    auto filename = fs::path(exe_path).stem().string();

    // Check filename against whitelist
    if (!kEmulatorBinaries.contains(filename)) {
        VLOG(1) << "File name " << filename << " is not in the whitelist.";
        return false;
    }

    VLOG(1) << "File name " << filename << " is in the whitelist.";
    return true;
}

bool EmulatorAdvertisement::IsPidAlive(fs::path my_file, fs::path discovery_file) {
    // Check to see if the process is alive
    std::string entry = discovery_file.filename().string();
    android::base::Pid pid = 0;
    if (file::is_file(discovery_file)) {
        if (sscanf(entry.c_str(), kLocationFormat, &pid) != 1) {
            // Not a discovery file..
            return false;
        }
    }

    if (file::is_dir(discovery_file)) {
        if (sscanf(entry.c_str(), "%d", &pid) != 1) {
            // Not a discovery dir..
            return false;
        }
    }

    VLOG(1) << "Checking liveness of " << pid;
    VLOG(1) << "I'm an emulator proc, my name is: " << Process::Me()->Exe() << " ("
            << Process::Me()->pid() << ")";

    // Maybe it is me, i'm alive!
    if (Process::Me()->pid() == pid) {
        return true;
    }

    auto proc = Process::FromPid(pid);
    if (!proc || !proc->IsAlive()) {
        // tsk tsk process is not alive.
        VLOG(1) << "pid: " << pid << " is not alive";
        return false;
    }

    auto name = proc->Exe();
    if (IsEmulatorProcess(name)) {
        // It's a qemu or emulator process.. Let's keep it alive.
        VLOG(1) << name << " is an emulator process, so the pid file is alive.";
        return true;
    }

    VLOG(1) << "No idea what " << name << " is.. you are dead.";
    return false;
}

EmulatorAdvertisement::EmulatorAdvertisement(fs::path discovery_directory,
                                             LivenessChecker liveness_checker)

        : liveness_checker_(std::move(liveness_checker))
        , shared_directory_(std::move(discovery_directory))
        , location_(shared_directory_ / absl::StrFormat(kLocationFormat, Process::Me()->pid())) {
    DCHECK(file::exists(shared_directory_));
}

EmulatorAdvertisement::~EmulatorAdvertisement() {
    Remove().IgnoreError();
    GarbageCollect();
}

int EmulatorAdvertisement::GarbageCollect() const {
    auto start = absl::Now();
    VLOG(1) << "Starting garbage collection of advertisement.";
    int collected = 0;
    for (const fs::path& entry : file::scan_dir(shared_directory_, true)) {
        VLOG(1) << "Checking: " << entry.string();
        if (!liveness_checker_(location_, entry)) {
            VLOG(1) << "Deleting " << entry.string();
            collected++;
            // Emulator is not running, or unreachable.
            if (file::is_file(entry)) {
                if (auto s = file::rm(entry); !s.ok()) {
                    LOG(WARNING) << "Unable to clean up stale emulator discovery file: '"
                                 << entry.string()
                                 << "'. Please consider removing it manually. Reason: " << s;
                }
            }
            if (file::is_dir(entry)) {
                if (auto s = file::rm_recursive(entry); !s.ok()) {
                    LOG(WARNING) << "Unable to clean up stale emulator discovery directory: '"
                                 << entry.string()
                                 << "'. Please consider removing it manually. Reason: " << s;
                }
            }
        }
    }
    auto duration = absl::Now() - start;
    VLOG(1) << "It took " << duration << "  to collect " << collected << " leftovers";
    return collected;
}

absl::StatusOr<std::vector<fs::path>> EmulatorAdvertisement::DiscoverRunningEmulators() const {
    VLOG(1) << "Scanning " << shared_directory_.string();
    if (!file::is_dir(shared_directory_)) {
        return absl::InternalError("Discovery directory is missing or not a directory");
    }
    std::vector<fs::path> discovered;
    for (const fs::path& entry : file::scan_dir(shared_directory_, true)) {
        if (entry != location_ && liveness_checker_(location_, entry)) {
            discovered.push_back(entry);
        }
    }

    return discovered;
}

absl::StatusOr<fs::path> EmulatorAdvertisement::DiscoverEmulatorWithProperties(
        const EmulatorProperties& props) const {
    auto discovered = DiscoverRunningEmulators();
    if (!discovered.ok()) {
        return discovered.status();
    }
    for (const fs::path& discovery_file : *discovered) {
        IniFile ini(discovery_file);
        if (!ini.Read()) continue;

        bool match = true;
        for (const auto& [key, val] : props) {
            match = match && ini.HasKey(key) && ini.GetString(key, "") == val;
        }
        if (match) return discovery_file;
    }

    return absl::NotFoundError("No matching emulator found");
}

absl::Status EmulatorAdvertisement::Remove() const {
    auto status = file::rm(location_);
    fs::path pid_dir = shared_directory_ / std::to_string(Process::Me()->pid());
    if (file::is_dir(pid_dir)) {
        VLOG(1) << "Deleting my pid dir " << pid_dir.string();

        if (auto s = file::rm_recursive(pid_dir); !s.ok()) {
            return s;
        }
    }
    VLOG(1) << "Deleted " << location_.string() << ", status: " << status;
    return status;
}

absl::StatusOr<fs::path> EmulatorAdvertisement::CreateJwkDirectory(std::string_view token) const {
    fs::path jwk_dir = shared_directory_ / std::to_string(Process::Me()->pid()) / "jwks" / token;
    if (auto s = file::mkdir_recursive(jwk_dir, 0700); !s.ok()) {
        return absl::UnavailableError(
                absl::StrCat("Failed to create jwk directory ", jwk_dir.string(), " error: ", s));
    }
    return jwk_dir;
}

absl::Status EmulatorAdvertisement::Write(const EmulatorProperties& config) const {
    fs::path temp_file = location_.string() + ".tmp";
    std::string content;
    for (const auto& elem : config) {
        absl::StrAppend(&content, elem.first, "=", elem.second, "\n");
    }

    if (auto s = file::CreatePrivateFileExclusive(temp_file, content); !s.ok()) {
        return s;
    }

    // Atomic swap to ensure the advertisement is always valid.
    if (auto s = file::mv_file(temp_file, location_); !s.ok()) {
        file::rm(temp_file).IgnoreError();
        return s;
    }

    LOG(INFO) << "Advertising in discovery file: " << location_.string();
#ifndef _WIN32
    // Protect the discovery file from system cleanup (e.g., systemd-tmpfiles).
    // We must either:
    // 1. Update the access time (atime) at least every 6 hours.
    // 2. Set the 'sticky' bit (S_ISVTX) to signal cleanup scripts to ignore it.
    if (auto s = file::chmod(location_, S_IRUSR | S_ISVTX | S_IWUSR); !s.ok()) {
        LOG(WARNING) << "Emulator Visibility Warning: Failed to protect the "
                        "discovery file. The emulator may disappear from Android "
                        "Studio if system cleanup scripts delete it. Error: "
                     << s;
    }
#endif
    return absl::OkStatus();
}

}  // namespace goldfish::discovery
