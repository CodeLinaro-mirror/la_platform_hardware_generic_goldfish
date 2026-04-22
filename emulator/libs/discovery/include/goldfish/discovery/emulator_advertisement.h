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
#include <functional>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace goldfish::discovery {

namespace fs = std::filesystem;

using EmulatorProperties = absl::flat_hash_map<std::string, std::string>;
using LivenessChecker = std::function<bool(fs::path, fs::path)>;

class EmulatorAdvertisement {
  public:
    EmulatorAdvertisement(std::filesystem::path discovery_directory = GetDiscoveryDirectory(),
                          LivenessChecker liveness_checker = EmulatorAdvertisement::IsPidAlive);
    ~EmulatorAdvertisement();
    EmulatorAdvertisement(EmulatorAdvertisement&&) = default;
    EmulatorAdvertisement& operator=(EmulatorAdvertisement&&) = default;
    EmulatorAdvertisement(const EmulatorAdvertisement&) = delete;
    EmulatorAdvertisement& operator=(const EmulatorAdvertisement&) = delete;

    /**
     * @brief Writes the ini file to the predefined location.
     * @return absl::OkStatus() if the write was successful, error otherwise.
     */
    [[nodiscard]] absl::Status Write(const EmulatorProperties& props) const;

    /**
     * @brief Creates the JWK directory in the discovery sub-folder for this process.
     * @param token The securely generated token prefix.
     * @return The path to the created JWK directory, or an error if creation fails.
     */
    absl::StatusOr<std::filesystem::path> CreateJwkDirectory(std::string_view token) const;

    /**
     * @brief Discovers all the advertisement files of active emulators, excluding
     * us.
     * @return A vector of paths to the discovery files of running emulators, or
     * error.
     */
    absl::StatusOr<std::vector<fs::path>> DiscoverRunningEmulators() const;

    /**
     * @brief Discovers the first advertisement file that matches the given
     * properties.
     * @param props The properties to match.
     * @return The path to the matching discovery file, or an error if not found
     *         or if discovery failed.
     */
    absl::StatusOr<fs::path> DiscoverEmulatorWithProperties(const EmulatorProperties& props) const;

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

  protected:
    // To allow for testing.

    /**
     * @brief Deletes stale advertisement files for dead processes.
     *
     * Deletes all ini files in the discovery directory and
     * directories for which no corresponding process exists.
     * @return The number of files deleted.
     */
    int GarbageCollect() const;

    static bool IsPidAlive(fs::path my_file, fs::path discovery_file);
    /**
     * @brief Removes the advertisement file from the file system.
     * @return absl::OkStatus() if the removal was successful, error otherwise.
     */
    absl::Status Remove() const;

  private:
    LivenessChecker liveness_checker_;
    fs::path shared_directory_;
    fs::path location_;
};

}  // namespace goldfish::discovery
