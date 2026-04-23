// Copyright 2023 The Android Open Source Project
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

namespace android::goldfish {

namespace fs = std::filesystem;
// Helper struct to hold static methods related to misc. configuration
// directories used by the emulator.
struct ConfigDirs {
    /**
     * @brief  Returns the user-specific directory containing Android-related
     *         configuration files.
     *
     * @details This function prioritizes the following locations:
     *   1. The value of the `ANDROID_EMULATOR_HOME` environment variable, if
     * defined.
     *   2. A sub-directory of the `ANDROID_SDK_HOME` environment variable, if
     * defined.
     *   3. A subdirectory of the user's home directory.
     *
     * @return fs::path The path to the user-specific Android configuration
     * directory.
     */
    static fs::path GetUserDirectory();

    /**
     * @brief Returns the root path containing all AVD sub-directories.
     *
     * @details This function follows a hierarchical search order:
     *   1. `$ANDROID_AVD_HOME`, if defined and valid.
     *   2. `$ANDROID_SDK_HOME/.android/avd`, if valid.
     *   3. If `$ANDROID_SDK_HOME` is defined but doesn't contain a valid AVD
     * root:
     *      - `$TEST_TMPDIR/.android/avd`, if valid.
     *      - `$USER_HOME/.android/avd`, if valid.
     *      - `$HOME/.android/avd`, if valid.
     *   4. Otherwise, a sub-directory 'avd' of the output of
     * `getUserDirectory()`.
     *
     * @return fs::path The path to the AVD root directory.
     */
    static fs::path GetAvdRootDirectory();

    /**
     * @brief Returns the path to the root of the Android SDK,优先env变量
     *                优先env变量.
     *
     * @details This function checks the following environment variables:
     *          - `ANDROID_HOME`
     *          - `ANDROID_SDK_ROOT`
     *
     * @param verbose Whether to print verbose log messages.
     *
     * @return fs::path The path to the Android SDK root directory.
     */
    static fs::path GetSdkRootDirectoryByEnv(bool verbose = false);

    /**
     * @brief Returns the path to the root of the Android SDK, by inferring it
     * from the path of the running emulator binary.
     *
     * @param launcher_dir path to launcher directory
     * @param verbose Whether to print verbose log messages.
     * @return fs::path The path to the Android SDK root directory.
     */
    static fs::path GetSdkRootDirectoryByPath(const fs::path& launcher_dir, bool verbose = false);

    /**
     * @brief Returns the default installation path to the root of the Android SDK if it is valid.
     *
     * @param verbose Whether to print verbose log messages.
     * @return fs::path The path to the Android SDK root directory.
     */
    static fs::path GetSdkRootDefault(bool verbose);

    /**
     * @brief Returns the path to the root of the Android SDK.
     *
     * @details This function combines the logic of `getSdkRootDirectoryByEnv()`
     *          and `getSdkRootDirectoryByPath()`:
     *   1. Prioritizes the `ANDROID_SDK_ROOT` environment variable, if defined
     * and valid.
     *   2. Otherwise, infers the SDK root from the running emulator binary's
     * path.
     *
     * @param launcher_dir path to launcher directory
     * @param verbose Whether to print verbose log messages.
     * @return fs::path The path to the Android SDK root directory.
     */
    static fs::path GetSdkRootDirectory(const fs::path& launcher_dir, bool verbose = false);

  private:
    // Check if the specified path is a valid AVD root path.
    // It is considered valid if it has an 'avd' subdirectory
    static bool IsValidAvdRoot(const fs::path& avd_path);

    // Check if the specified path is a valid SDK root path.
    // It is considered valid if it has a 'platforms' subdirectory
    // and a 'platform-tools' subdirectory.
    static bool IsValidSdkRoot(const fs::path& root_path, bool verbose = false);

    static fs::path GetAvdRootDirectoryWithPrefsRoot(const fs::path& path);
};

}  // namespace android::goldfish
