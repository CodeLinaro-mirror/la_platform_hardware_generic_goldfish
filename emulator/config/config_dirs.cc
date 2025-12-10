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
#include "android/goldfish/config_dirs.h"

#include <cassert>
#include <filesystem>
#include <string_view>

#include "absl/log/log.h"

#include "android/base/file/file.h"
#include "android/base/system.h"

namespace android::goldfish {

namespace fs = std::filesystem;

using android::base::System;
// Name of the Android configuration directory under $HOME.
static const std::string_view kAndroidSubDir = ".android";
// Subdirectory for AVD data files.
static const std::string_view kAvdSubDir = "avd";

// static
auto ConfigDirs::getUserDirectory() -> fs::path {
    fs::path home = System::get()->envGet("ANDROID_EMULATOR_HOME");
    if (!home.empty()) {
        return home;
    }

    // New key: ANDROID_PREFS_ROOT
    home = System::get()->envGet("ANDROID_PREFS_ROOT");
    if (!home.empty()) {
        // In v1.9 emulator was changed to use $ANDROID_SDK_HOME/.android
        // directory, but Android Studio has always been using $ANDROID_SDK_HOME
        // directly. Put a workaround here to make sure it works both ways,
        // preferring the one from AS.
        auto homeNewWay = fs::path(home) / kAndroidSubDir;
        return base::file::is_dir(homeNewWay) ? homeNewWay : home;
    }  // Old key that is deprecated (ANDROID_SDK_HOME)
    home = System::get()->envGet("ANDROID_SDK_HOME");
    if (!home.empty()) {
        auto homeOldWay = fs::path(home) / kAndroidSubDir;
        return base::file::exists(homeOldWay) ? homeOldWay : home;
    }

    home = android::base::System::get()->getHomeDirectory();
    if (home.empty()) {
        return fs::temp_directory_path();
    }
    return home / kAndroidSubDir;
}

// static
auto ConfigDirs::getAvdRootDirectory() -> fs::path {
    // The search order here should match that in AndroidLocation.java
    // in Android Studio. Otherwise, Studio and the Emulator may find
    // different AVDs. Or one may find an AVD when the other doesn't.
    fs::path avdRoot = System::get()->envGet("ANDROID_AVD_HOME");
    if (!avdRoot.empty() && base::file::is_dir(avdRoot)) {
        return avdRoot;
    }

    // No luck with ANDROID_AVD_HOME, try ANDROID_PREFS_ROOT/ANDROID_SDK_HOME
    avdRoot = getAvdRootDirectoryWithPrefsRoot(System::get()->envGet("ANDROID_PREFS_ROOT"));
    if (!avdRoot.empty()) {
        return avdRoot;
    }
    avdRoot = getAvdRootDirectoryWithPrefsRoot(System::get()->envGet("ANDROID_SDK_HOME"));
    if (!avdRoot.empty()) {
        return avdRoot;
    }  // ANDROID_PREFS_ROOT/ANDROID_SDK_HOME is defined but bad. In this case,
    // Android Studio tries $TEST_TMPDIR, $USER_HOME, and
    // $HOME. We'll do the same.
    avdRoot = System::get()->envGet("TEST_TMPDIR");
    if (!avdRoot.empty()) {
        avdRoot = fs::path(avdRoot) / kAndroidSubDir;
        if (isValidAvdRoot(avdRoot)) {
            return fs::path(avdRoot) / kAvdSubDir;
        }
    }
    avdRoot = System::get()->envGet("USER_HOME");
    if (!avdRoot.empty()) {
        avdRoot = fs::path(avdRoot) / kAndroidSubDir;
        if (isValidAvdRoot(avdRoot)) {
            return fs::path(avdRoot) / kAvdSubDir;
        }
    }
    avdRoot = System::get()->envGet("HOME");
    if (!avdRoot.empty()) {
        avdRoot = fs::path(avdRoot) / kAndroidSubDir;
        if (isValidAvdRoot(avdRoot)) {
            return fs::path(avdRoot) / kAvdSubDir;
        }
    }

    // No luck with ANDROID_AVD_HOME, ANDROID_SDK_HOME,
    // TEST_TMPDIR, USER_HOME, or HOME. Try even more.
    return getUserDirectory() / kAvdSubDir;
}

// static
auto ConfigDirs::getSdkRootDirectoryByEnv(bool verbose) -> fs::path {
    LOG_IF(INFO, verbose) << "checking ANDROID_HOME for valid sdk root.";
    std::string sdkRoot = System::get()->envGet("ANDROID_HOME");
    LOG_IF(INFO, verbose) << "ANDROID_HOME: " << sdkRoot;

    if (!sdkRoot.empty() && isValidSdkRoot(sdkRoot, verbose)) {
        return sdkRoot;
    }

    LOG_IF(INFO, verbose) << "checking ANDROID_SDK_ROOT for valid sdk root.";

    // ANDROID_HOME is not good. Try ANDROID_SDK_ROOT.
    sdkRoot = System::get()->envGet("ANDROID_SDK_ROOT");
    if (static_cast<unsigned int>(!sdkRoot.empty()) != 0U) {
        // Unquote a possibly "quoted" path.
        if (sdkRoot[0] == '"') {
            assert(sdkRoot.back() == '"');
            sdkRoot.erase(0, 1);
            sdkRoot.pop_back();
        }
        if (isValidSdkRoot(sdkRoot, verbose)) {
            return sdkRoot;
        }
    }

    LOG_IF(WARNING, verbose) << "ANDROID_SDK_ROOT is missing.";
    return {};
}

auto ConfigDirs::getSdkRootDirectoryByPath(const fs::path& launcher_dir, bool verbose) -> fs::path {
    fs::path sdkRoot = launcher_dir;
    for (int i = 0; i < 3; ++i) {
        sdkRoot = sdkRoot.parent_path();
        LOG_IF(INFO, verbose) << "guessed sdk root: " << sdkRoot.string();
        if (isValidSdkRoot(sdkRoot, verbose)) {
            return sdkRoot;
        }
        LOG_IF(INFO, verbose) << "guessed sdk root " << sdkRoot.string()
                              << " does not seem to be valid";
    }
    LOG_IF(WARNING, verbose) << "invalid sdk root:" << sdkRoot.string();
    return {};
}

// static
auto ConfigDirs::getSdkRootDirectory(const fs::path& launcher_dir, bool verbose) -> fs::path {
    auto sdkRoot = getSdkRootDirectoryByEnv(verbose);
    if (!sdkRoot.empty()) {
        return sdkRoot;
    }

    LOG_IF(WARNING, verbose) << "Cannot find valid sdk root from environment "
                                "variable ANDROID_HOME nor ANDROID_SDK_ROOT,"
                                "Try to infer from emulator's path";
    // Otherwise, infer from the path of the emulator's binary.
    return getSdkRootDirectoryByPath(launcher_dir, verbose);
}

// static
auto ConfigDirs::isValidSdkRoot(const fs::path& rootPath, bool verbose) -> bool {
    if (rootPath.empty()) {
        LOG_IF(WARNING, verbose) << "empty sdk root";
        return false;
    }

    if (!base::file::is_dir(rootPath) || !base::file::can_read(rootPath)) {
        if (verbose) {
            if (!base::file::is_dir(rootPath)) {
                LOG(WARNING) << rootPath << " is not a directory, and cannot be sdk root";
            } else if (!base::file::can_read(rootPath)) {
                LOG(WARNING) << rootPath << " is not readable, and cannot be sdk root";
            }
        }
        return false;
    }
    fs::path platformsPath = fs::path(rootPath) / "platforms";
    if (!base::file::is_dir(rootPath) || !base::file::can_read(rootPath)) {
        LOG_IF(WARNING, verbose) << "platforms subdirectory is missing under " << rootPath
                                 << ", please install it";
        return false;
    }
    fs::path platformToolsPath = fs::path(rootPath) / "platform-tools";
    if (!base::file::is_dir(platformToolsPath)) {
        LOG_IF(WARNING, verbose) << "platform-tools subdirectory is missing under " << rootPath
                                 << ", please install it";
        return false;
    }

    return true;
}

// static
auto ConfigDirs::isValidAvdRoot(const fs::path& avdPath) -> bool {
    if (avdPath.empty()) {
        return false;
    }
    if (!base::file::is_dir(avdPath) || !base::file::can_read(avdPath)) {
        return false;
    }
    fs::path avdAvdPath = avdPath / "avd";
    return (base::file::is_dir(avdAvdPath) && base::file::can_read(avdAvdPath));
}

auto ConfigDirs::getAvdRootDirectoryWithPrefsRoot(const fs::path& path) -> fs::path {
    if (path.empty()) {
        return {};
    }

    // ANDROID_PREFS_ROOT is defined
    if (isValidAvdRoot(path)) {
        // ANDROID_PREFS_ROOT is good
        return path / kAvdSubDir;
    }

    fs::path avdRoot = path / kAndroidSubDir;
    if (isValidAvdRoot(avdRoot)) {
        // ANDROID_PREFS_ROOT/.android is good
        return avdRoot / kAvdSubDir;
    }

    return {};
}

using discovery_dir = struct discovery_dir {
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

static auto getAlternativeRoot() -> fs::path {
#ifdef __linux__
    auto uid = getuid();
    auto discovery = fs::path("/run/user/") / std::to_string(uid);
    if (base::file::exists(discovery)) {
        return discovery;
    }
#endif

    // Reverting to the standard emulator user directories
    return ConfigDirs::getUserDirectory();
}

auto ConfigDirs::getDiscoveryDirectory() -> fs::path {
    fs::path root = System::get()->envGet(discovery.root_env);
    if (root.empty()) {
        // Reverting to the alternative root if these environment variables do
        // not exist.
        LOG(WARNING) << "Using fallback path for the emulator registration directory.";
        root = getAlternativeRoot();
    } else {
        root = root / discovery.subdir;
    }
    std::error_code ec;

    auto desired_directory = root / "avd" / "running";
    if (!base::file::exists(desired_directory)) {
        if (auto s = base::file::mkdir_recursive(desired_directory, 0755); !s.ok()) {
            LOG(WARNING) << "Unable to create directories: " << desired_directory << " due to " << s;
        }
    } else {
        base::file::chmod(desired_directory, 0755);
    }
    return desired_directory;
}

}  // namespace android::goldfish
