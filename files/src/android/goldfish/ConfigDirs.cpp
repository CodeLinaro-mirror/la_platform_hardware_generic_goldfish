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
#include "android/goldfish/ConfigDirs.h"

#include <cassert>
#include <filesystem>
#include <string_view>

#include "aemu/base/logging/Log.h"
#include "aemu/base/system/System.h"
#include "android/base/system/System.h"

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
    return fs::is_directory(homeNewWay) ? homeNewWay : home;
  } // Old key that is deprecated (ANDROID_SDK_HOME)
  home = System::get()->envGet("ANDROID_SDK_HOME");
  if (!home.empty()) {
    auto homeOldWay = fs::path(home) / kAndroidSubDir;
    return System::get()->pathExists(homeOldWay) ? homeOldWay : home;
  }

  home = android::base::System::get()->getHomeDirectory();
  if (home.empty()) {
    return fs::temp_directory_path();
  }
  return home / kAndroidSubDir;
}

// static
auto ConfigDirs::getAvdRootDirectory() -> fs::path {
  System *system = System::get();
  // The search order here should match that in AndroidLocation.java
  // in Android Studio. Otherwise, Studio and the Emulator may find
  // different AVDs. Or one may find an AVD when the other doesn't.
  std::string avdRoot = System::get()->envGet("ANDROID_AVD_HOME");
  if (!avdRoot.empty() && system->pathIsDir(avdRoot)) {
    return avdRoot;
  }

  // No luck with ANDROID_AVD_HOME, try ANDROID_PREFS_ROOT/ANDROID_SDK_HOME
  avdRoot = getAvdRootDirectoryWithPrefsRoot(
      System::get()->envGet("ANDROID_PREFS_ROOT"));
  if (!avdRoot.empty()) {
    return avdRoot;
  }
  avdRoot = getAvdRootDirectoryWithPrefsRoot(
      System::get()->envGet("ANDROID_SDK_HOME"));
  if (!avdRoot.empty()) {
    return avdRoot;
  } // ANDROID_PREFS_ROOT/ANDROID_SDK_HOME is defined but bad. In this case,
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
  if (verbose) {
    LOG(INFO) << "checking ANDROID_HOME for valid sdk root.";
  }
  std::string sdkRoot = System::get()->envGet("ANDROID_HOME");
  if (verbose) {
    dinfo("ANDROID_HOME: %s", sdkRoot);
  }
  if (!sdkRoot.empty() && isValidSdkRoot(sdkRoot, verbose)) {
    return sdkRoot;
  }

  if (verbose) {
    LOG(INFO) << "checking ANDROID_SDK_ROOT for valid sdk root.";
  }
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
  } else if (verbose) {
    dwarning("ANDROID_SDK_ROOT is missing.");
  }

  return {};
}

auto ConfigDirs::getSdkRootDirectoryByPath(bool verbose) -> fs::path {
  auto parts = System::get()->getLauncherDirectory();

  fs::path sdkRoot = fs::path(parts);
  for (int i = 0; i < 3; ++i) {
    sdkRoot = sdkRoot.parent_path();
    if (verbose) {
      dinfo("guessed sdk root: %s", sdkRoot);
    }
    if (isValidSdkRoot(sdkRoot, verbose)) {
      return sdkRoot;
    }
    if (verbose) {
      LOG(INFO) << "guessed sdk root " << sdkRoot
                << " does not seem to be valid";
    }
  }
  if (verbose) {
    LOG(WARNING) << "invalid sdk root:" << sdkRoot;
  }
  return {};
}

// static
auto ConfigDirs::getSdkRootDirectory(bool verbose) -> fs::path {
  std::string sdkRoot = getSdkRootDirectoryByEnv(verbose);
  if (!sdkRoot.empty()) {
    return sdkRoot;
  }

  if (verbose) {
    dwarning("Cannot find valid sdk root from environment "
             "variable ANDROID_HOME nor ANDROID_SDK_ROOT,"
             "Try to infer from emulator's path");
  }
  // Otherwise, infer from the path of the emulator's binary.
  return getSdkRootDirectoryByPath(verbose);
}

// static
auto ConfigDirs::isValidSdkRoot(const fs::path &rootPath,
                                bool verbose) -> bool {
  if (rootPath.empty()) {
    if (verbose) {
      dwarning("empty sdk root");
    }
    return false;
  }

  System *system = System::get();
  if (!system->pathIsDir(rootPath) || !system->pathCanRead(rootPath)) {
    if (verbose) {
      if (!system->pathIsDir(rootPath)) {
        dwarning("%s is not a directory, and cannot be sdk root", rootPath);
      } else if (!system->pathCanRead(rootPath)) {
        dwarning("%s is not readable, and cannot be sdk root", rootPath);
      }
    }
    return false;
  }
  fs::path platformsPath = fs::path(rootPath) / "platforms";
  if (!system->pathIsDir(rootPath) || !system->pathCanRead(rootPath)) {
    if (verbose) {
      LOG(WARNING) << "platforms subdirectory is missing under " << rootPath
                   << ", please install it";
    }
    return false;
  }
  fs::path platformToolsPath = fs::path(rootPath) / "platform-tools";
  if (!system->pathIsDir(platformToolsPath)) {
    if (verbose) {
      LOG(WARNING) << "platform-tools subdirectory is missing under "
                   << rootPath << ", please install it";
    }
    return false;
  }

  return true;
}

// static
auto ConfigDirs::isValidAvdRoot(const fs::path &avdPath) -> bool {
  if (avdPath.empty()) {
    return false;
  }
  System *system = System::get();
  if (!system->pathIsDir(avdPath) || !system->pathCanRead(avdPath)) {
    return false;
  }
  fs::path avdAvdPath = avdPath / "avd";
  return (system->pathIsDir(avdAvdPath) && system->pathCanRead(avdAvdPath));
}

auto ConfigDirs::getAvdRootDirectoryWithPrefsRoot(const fs::path &path)
    -> fs::path {
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
  const char *root_env;
  const char *subdir;
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
  if (System::get()->pathExists(discovery)) {
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
    LOG(WARNING)
        << "Using fallback path for the emulator registration directory.";
    root = getAlternativeRoot();
  } else {
    root = root / discovery.subdir;
  }

  auto desired_directory = root / "avd" / "running";
  auto recomposed = fs::canonical(desired_directory);
  if (!fs::exists(recomposed)) {
    std::error_code ec;
    if (!fs::create_directories(recomposed, ec)) {
      LOG(WARNING) << "Unable to create directories: " << recomposed
                   << " due to " << ec.message();
    }
    fs::permissions(recomposed, fs::perms::owner_all, fs::perm_options::remove);
  }
  return recomposed;
}
} // namespace android::goldfish
