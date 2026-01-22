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

#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <utility>
#include <vector>

#include "absl/log/log.h"

#include "aemu/base/StringFormat.h"
#include "android/process/process.h"
#include "aemu/base/sockets/ScopedSocket.h"
#include "aemu/base/sockets/SocketUtils.h"
#include "android/base/file/file.h"
#include "android/goldfish/ini_file.h"
#include "emulator_advertisement.h"

namespace android {
namespace goldfish {

#define DEBUG 0
/* set  for very verbose debugging */
#if DEBUG <= 1
#define DD(...) (void)0
#else
#include "android/utils/debug.h"
#define DD(...) dinfo(__VA_ARGS__);
#endif

namespace fs = std::filesystem;
using android::base::Process;
static const char* location_format = "pid_%d.ini";

static bool canConnectToPort(int64_t port) {
    if (port == 0) {
        return false;
    }
    base::ScopedSocket fd(base::socketTcp6LoopbackClient(port));
    if (fd.valid()) {
        return true;
    }
    fd = android::base::socketTcp4LoopbackClient(port);
    if (fd.valid()) {
        return true;
    }

    return false;
}

// Liveness checker that tries to load the discovery file
// and tries to see if any of the declared ports are accessible
// and validates that the ports do not point to me.
bool OpenPortChecker::isAlive(fs::path myFile, fs::path discoveryFile) const {
    if (myFile == discoveryFile) {
        return true;
    }

    DD("Checking liveness of entry %s", discoveryFile.string().c_str());
    IniFile ini(discoveryFile);
    IniFile me(myFile);
    if (!ini.Read()) {
        DD("Invalid ini file: %s", discoveryFile.string().c_str());
        return false;
    }

    if (!base::file::exists(myFile) || !me.Read()) {
        DD("Invalid ini file: %s (that's ok)", myFile.string().c_str());
    }

    // Check if we can connect to any of the ports that are defined in the
    // discovery file.. If we can, then we are alive..
    for (const auto& port : {"grpc.port", "port.serial", "port.adb"}) {
        auto checkPort = ini.GetInt64(port, 0);
        auto myPort = me.GetInt64(port, 0);

        if (myPort != 0 && myPort == checkPort) {
            DD("Imposter! Your ini file contains ports that are owned by me!");
            return false;
        }

        if (checkPort != 0 && canConnectToPort(checkPort)) {
            DD("Able to connect to port %d", checkPort);
            return true;
        }
    }

    return false;
}

bool PidChecker::isAlive(fs::path myFile, fs::path discoveryFile) const {
    // Check to see if the process is alive
    std::string entry = discoveryFile.filename().string();
    int pid = 0;
    if (base::file::is_file(discoveryFile)) {
        if (sscanf(entry.c_str(), location_format, &pid) != 1) {
            // Not a discovery file..
            return false;
        }
    }

    if (base::file::is_dir(discoveryFile)) {
        if (sscanf(entry.c_str(), "%d", &pid) != 1) {
            // Not a discovery dir..
            return false;
        }
    }

    DD("Checking liveness of %d", pid);
    DD("I'm an emulator proc, my name is: %s (%d)", Process::me()->exe().c_str(),
       Process::me()->pid());

    // Maybe it is me, i'm alive!
    if (Process::Me()->pid() == pid) {
        return true;
    }

    auto proc = Process::FromPid(pid);
    if (!proc) {
        // tsk tsk process is not alive.
        DD("pid: %d is not alive", pid);
        return false;
    }

    auto name = proc->Exe();
    if (name.find("emulator") != std::string::npos ||
        name.find("qemu-system-") != std::string::npos) {
        // It's a qemu or emulator process.. Let's keep it alive.
        DD("%s is an emulator process, so the pid file is alive.", name.c_str());
        return true;
    }

    DD("No idea what %s is.. you are dead.", name.c_str());
    return false;
}

EmulatorAdvertisement::EmulatorAdvertisement(
        EmulatorProperties config, fs::path discoveryDirectory,
        std::unique_ptr<EmulatorLivenessStrategy> livenessChecker)
        : mStudioConfig(std::move(config))
        , mSharedDirectory(std::move(discoveryDirectory))
        , mLivenessChecker(std::move(livenessChecker)) {
    assert(base::file::exists(mSharedDirectory));
}

EmulatorAdvertisement::~EmulatorAdvertisement() {
    garbageCollect();
}

int EmulatorAdvertisement::garbageCollect() const {
    auto start = std::chrono::high_resolution_clock::now();
    DD("Starting garbage collection of advertisement.");
    int collected = 0;
    for (const fs::path& entry : base::file::scan_dir(mSharedDirectory, true)) {
        DD("Checking: %s", entry.string().c_str());
        if (!mLivenessChecker->isAlive(location(), entry)) {
            DD("Deleting %s", entry.string().c_str());
            collected++;
            // Emulator is not running, or unreachable.
            if (base::file::is_file(entry)) {
                base::file::rm(entry).IgnoreError();
            }
            if (base::file::is_dir(entry)) {
                base::file::rm_recursive(entry).IgnoreError();
            }
        }
    }
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - start);
    DD("It took %lld ms to collect %d leftovers", duration.count(), collected);
    return collected;
}

std::vector<fs::path> EmulatorAdvertisement::discoverRunningEmulators() const {
    DD("Scanning %s", mSharedDirectory.string().c_str());
    std::vector<fs::path> discovered;
    for (const fs::path& entry : base::file::scan_dir(mSharedDirectory, true)) {
        if (entry != location() && mLivenessChecker->isAlive(location(), entry)) {
            discovered.push_back(entry);
        }
    }

    return discovered;
}

fs::path EmulatorAdvertisement::discoverEmulatorWithProperties(
        const EmulatorProperties& props) const {
    for (const fs::path& discoveryFile : discoverRunningEmulators()) {
        IniFile ini(discoveryFile);
        if (!ini.Read()) continue;

        bool match = true;
        for (const auto& [key, val] : props) {
            match = match && ini.HasKey(key) && ini.GetString(key, "") == val;
        }
        if (match) return discoveryFile;
    }

    return "";
}

void EmulatorAdvertisement::remove() const {
    base::file::rm(location()).IgnoreError();
    fs::path pid_dir = mSharedDirectory / std::to_string(Process::Me()->pid());
    if (base::file::is_dir(pid_dir)) {
        DD("Deleting my pid dir %s", pid_dir.string().c_str());
        base::file::rm_recursive(pid_dir).IgnoreError();
    }
}

fs::path EmulatorAdvertisement::location() const {
    auto pid = Process::Me()->pid();
    std::string pidfile = android::base::StringFormat(location_format, pid);
    return mSharedDirectory / pidfile;
}

bool EmulatorAdvertisement::write() const {
    fs::path pidFile = location();
    if (base::file::exists(pidFile)) {
        LOG(WARNING) << "Overwriting existing discovery file: " << pidFile;
    }
    LOG(INFO) << "Advertising in: " << pidFile;
    auto shareFile = std::ofstream(pidFile);
    for (const auto& elem : mStudioConfig) {
        shareFile << elem.first << "=" << elem.second << "\n";
    }
    shareFile.flush();
    shareFile.close();

#ifndef _WIN32
    // To ensure that your files are not removed, they should have their access
    // time timestamp modified at least once every 6 hours of monotonic time or
    // the 'sticky' bit should be set on the file.
    chmod(pidFile.string().c_str(), S_IRUSR | S_ISVTX | S_IWUSR);
#endif
    return !shareFile.bad();
}

}  // namespace goldfish
}  // namespace android
