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

#pragma once

#include <filesystem>
#include <memory>         // for make_unique, unique_ptr
#include <string>         // for string, hash, operator==
#include <unordered_map>  // for unordered_map
#include <vector>         // for vector

namespace android {
namespace goldfish {

namespace fs = std::filesystem;

// A simple emulator configuration that can be shared with external processes.
// Properties are simple string pairs that are written to disk as ini files with
// "key=value" entries.
using EmulatorProperties = std::unordered_map<std::string, std::string>;

// Strategy pattern for checking if the emulator corresponding
// the discovery file is actually alive.
//
// Mainly here so you can write proper unit tests.
class EmulatorLivenessStrategy {
  public:
    virtual ~EmulatorLivenessStrategy() {};
    virtual bool isAlive(fs::path myFile, fs::path discoveryFile) const = 0;
};

// Liveness checker that tries to load the discovery file
// and tries to see if any of the declared ports are accessible
//
/// NOTE: this can be very slow, so best not to use it.
class OpenPortChecker : public EmulatorLivenessStrategy {
  public:
    bool isAlive(fs::path myFile, fs::path discoveryFile) const override;
};

// Liveness checker that tries to load the discovery file
// and tries to see that the pid exists and has the proper name
// (i.e. contains: "emulator", or "qemu-system-")
class PidChecker : public EmulatorLivenessStrategy {
  public:
    bool isAlive(fs::path myFile, fs::path discoveryFile) const override;
};

// External services might need to know where to find information about
// running emulators. An EmulatorAdvertisement can write an
// EmulatorConfiguration dictionary to a
// predefined location.
//
// The location is defined as follows:
//
// <user-specific_tmp_directory>/avd/running  where the
// user-specific_tmp_directory is:
//  - $XDG_RUNTIME_DIR on Linux,
//  - $HOME/Library/Caches/TemporaryItems on Mac
//  - %LOCALAPPDATA%/Temp on Windows.
//
// The file will be named "pid_%d_info.ini" where %d is
// the process id of the emulator.
class EmulatorAdvertisement {
  public:
    EmulatorAdvertisement(EmulatorProperties config, std::filesystem::path discoveryDirectory,
                          std::unique_ptr<EmulatorLivenessStrategy> livenessChecker =
                                  std::make_unique<PidChecker>());
    ~EmulatorAdvertisement();

    // Writes the ini file to the location.
    bool write() const;

    // Removes the file from the file system.
    void remove() const;

    // Deletes all ini files in <user-specific_tmp_directory>/avd/running and
    // directories <user-specific_tmp_directory>/avd/running/<pid> for
    // which no corresponding process exists. returns the number of files
    // deleted.
    int garbageCollect() const;

    // Discovers all the advertisement files of active emulators, excluding us.
    std::vector<fs::path> discoverRunningEmulators() const;

    // Discovers the first advertisment file of active emulators that
    // has the set of props available.
    fs::path discoverEmulatorWithProperties(const EmulatorProperties& props) const;

    /**
     * @brief Returns the path to the Android Studio emulator discovery directory.
     *
     * This function returns the path to the directory used by Android Studio to
     * detect running emulators. If the directory does not exist, it will be
     * created with 0700 permissions.
     *
     * @details The user-specific temporary directory is determined based on
     * platform-specific conventions, following this order of preference:
     *
     * **Linux:**
     *   - `$XDG_RUNTIME_DIR`
     *   - `/run/user/$UID`
     *   - `$HOME/.android`
     *
     * **MacOS:**
     *   - `~/Library/Caches/TemporaryItems`
     *   - `$HOME/.android`
     *
     * **Windows:**
     *   - `%LOCALAPPDATA%/Temp`
     *   - `%USERPROFILE%/.android`
     *
     * @return fs::path The path to the Android Studio emulator discovery
     * directory.
     */
    static fs::path GetDiscoveryDirectory();

    EmulatorAdvertisement(EmulatorAdvertisement&&) = default;
    EmulatorAdvertisement& operator=(EmulatorAdvertisement&&) = default;
    EmulatorAdvertisement(const EmulatorAdvertisement&) = delete;
    EmulatorAdvertisement& operator=(const EmulatorAdvertisement&) = delete;

  private:
    // The location where the .ini file will be written to.
    fs::path location() const;

    EmulatorProperties mStudioConfig;
    fs::path mSharedDirectory;
    std::unique_ptr<EmulatorLivenessStrategy> mLivenessChecker;
};

}  // namespace goldfish
}  // namespace android
