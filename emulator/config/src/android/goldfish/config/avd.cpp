// Copyright 2024 The Android Open Source Project
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
#include "android/goldfish/config/avd.h"

#include <android/goldfish/config/hardware_config.h>
#include <filesystem>
#include <memory>
#include <regex>
#include <unordered_map>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "aemu/base/files/IniFile.h"
#include "android/base/system/System.h"
#include "android/goldfish/config/config_dirs.h"
#include "android/goldfish/config/keys.h"
#include "host-common/hw-config.h"

/* technical note on how all of this is supposed to work:
 *
 * Each AVD corresponds to a "content directory" that is used to
 * store persistent disk images and configuration files. Most remarkable
 * are:
 *
 * - a "config.ini" file used to hold configuration information for the
 *   AVD
 *
 * - mandatory user data image ("userdata-qemu.img") and cache image
 *   ("cache.img")
 *
 * - optional mutable system image ("system-qemu.img"), kernel image
 *   ("kernel-qemu") and read-only ramdisk ("ramdisk.img")
 *
 * When starting up an AVD, the emulator looks for relevant disk images
 * in the content directory. If it doesn't find a given image there, it
 * will try to search in the list of system directories listed in the
 * 'config.ini' file through one of the following (key,value) pairs:
 *
 *    images.sysdir.1 = <first search path>
 *    images.sysdir.2 = <second search path>
 *
 * The search paths can be absolute, or relative to the root SDK installation
 * path (which is determined from the emulator program's location, or from the
 * ANDROID_SDK_ROOT environment variable).
 *
 * Individual image disk search patch can be over-riden on the command-line
 * with one of the usual options.
 */
namespace android::goldfish {

using android::base::System;
using PropertyList = const std::array<std::string, 3>;
using DeviceType = Avd::DeviceType;

static const std::string_view
    _imageFileNames[static_cast<int>(Avd::ImageType::AVD_IMAGE_MAX)] = {
#define _AVD_IMG(x, y, z) y,
        AVD_IMAGE_LIST
#undef _AVD_IMG
};

static std::string getIconForDeviceType(DeviceType flavor) {
  switch (flavor) {
  case DeviceType::kPhone:
    return "📱"; // 📱 (Smartphone)
  case DeviceType::kTv:
    return "📺"; // 📺 (Television)
  case DeviceType::kWear:
    return "⌚️"; // ⌚️ (Smartwatch)
  case DeviceType::kAndroidAuto:
    return "🚗"; // 🚗 (Car)
  case DeviceType::kDesktop:
    return "🖥️"; // 🖥️ (Desktop computer)
  default:
    return "🤷"; // 🤷 (Unknown)
  }
}

Avd::CpuArchitecture Avd::detectArchitecture() const {
  auto abi = mConfig->getString("abi.type", "unknown");
  if (absl::StrContains(abi, "x86")) {
    return CpuArchitecture::kX86;
  }

  if (absl::StrContains(abi, "arm")) {
    return CpuArchitecture::kArm;
  }

  return CpuArchitecture::kUnknown;
}

Avd::Avd(Avd &&other) noexcept
    : mContentPath(std::move(other.mContentPath)),
      mTarget(std::move(other.mTarget)), mConfig(std::move(other.mConfig)),
      mName(std::move(other.mName)), mHwCfg(std::move(other.mHwCfg)) {}

bool Avd::hasEncryptionKey() const {
  return getSystemImagePath(Avd::ImageType::ENCRYPTIONKEY).ok();
}

absl::StatusOr<Avd> Avd::fromName(std::string name) {
  auto directory_path = ConfigDirs::getAvdRootDirectory();
  return Avd::parse(directory_path / (name + ".ini"));
}

DeviceType Avd::getDeviceType() const {
  DeviceType res = DeviceType::kUnknown;

  const std::unordered_map<std::string, DeviceType> labelMap{
      {"phone", DeviceType::kPhone},     {"atv", DeviceType::kTv},
      {"wear", DeviceType::kWear},       {"aw", DeviceType::kWear},
      {"car", DeviceType::kAndroidAuto}, {"pc", DeviceType::kDesktop}};

  const PropertyList props = {"ro.product.name", "ro.product.system.name",
                              "ro.build.flavor"};

  auto buildprop = getSystemImagePath(Avd::ImageType::BUILDPROP);
  if (!buildprop.ok()) {
    dwarning(
        "Unable to retrieve image path: %s, using unknown avd device type.",
        buildprop.status().message());
    return DeviceType::kUnknown;
  }

  if (!System::get()->pathExists(*buildprop) ||
      !System::get()->pathCanRead(*buildprop)) {
    dwarning("Unable to read build properties: %s, using unknown device type.",
             buildprop->string());
    return DeviceType::kUnknown;
  }
  IniFile buildIni(*buildprop);
  buildIni.read();

  for (const auto &prop : props) {
    if (!buildIni.hasKey(prop)) {
      continue;
    }

    auto build = buildIni.getString(prop, "_unused");
    for (const auto &[key, val] : labelMap) {
      if (build.find(key) != std::string::npos) {
        return val;
      }
    }
  }

  // Likely unknown.
  return res;
}

fs::path Avd::getImageFilenameByType(Avd::ImageType imgType) const {
  return _imageFileNames[static_cast<uint8_t>(imgType)];
}

absl::StatusOr<fs::path> Avd::getImageFilePath(Avd::ImageType imgType) const {
  auto possible = mContentPath / _imageFileNames[static_cast<uint8_t>(imgType)];
  if (System::get()->pathIsFile(possible) &&
      System::get()->pathCanRead(possible)) {
    return possible;
  }
  dprint("Did not find %s in %s, falling back to system path",
         possible.string(), mContentPath.string());
  return getSystemImagePath(imgType);
}

absl::StatusOr<fs::path> Avd::getSystemImagePath(Avd::ImageType imgType) const {
  auto sdk = ConfigDirs::getSdkRootDirectory();
  fs::path path = "no-sysimg";
  std::string key;
  for (int n = 0; n < MAX_SEARCH_PATHS; n++) {
    key = absl::StrFormat("%s%d", SEARCH_PREFIX, n);
    if (!mConfig->hasKey(key)) {
      continue;
    }
    path = sdk / mConfig->getString(key, "unused") /
           _imageFileNames[static_cast<uint8_t>(imgType)];

    if (System::get()->pathExists(path) && System::get()->pathCanRead(path)) {
      return path;
    }
  }
  return absl::NotFoundError(
      absl::StrFormat("Path %s specified in %s does not exist (key=%s)",
                      path.string(), mConfig->getBackingFile().string(), key));
}

std::string Avd::details() const {
  auto icon = getIconForDeviceType(getDeviceType());
  return absl::StrFormat("%-45s  - (%4dx%4d) %s", mName, mHwCfg.hw_lcd_width,
                         mHwCfg.hw_lcd_height, icon);
}

Avd::Avd(fs::path content_path, std::unique_ptr<IniFile> target,
         std::unique_ptr<IniFile> config, std::string name)
    : mContentPath(content_path), mTarget(std::move(target)),
      mConfig(std::move(config)), mName(name) {
  mHwCfg.load(this, mConfig.get());
}

absl::StatusOr<Avd> Avd::parse(fs::path ini_file) {
  auto sys = System::get();
  if (!sys->pathExists(ini_file) || !sys->pathCanRead(ini_file)) {
    return absl::NotFoundError(
        absl::StrCat("No access to: ", System::pathAsString(ini_file)));
  }

  auto ini = std::make_unique<IniFile>(ini_file);
  if (!ini->read()) {
    return absl::InternalError(absl::StrCat("Unable to parse ini file: ",
                                            System::pathAsString(ini_file)));
  }

  // Extract the avd name from the .ini file.
  std::string name = System::pathAsString(ini_file.stem());

  fs::path content_path = fs::path(ini->get<std::string>("path", ""));
  if (!sys->pathExists(content_path) || !sys->pathCanRead(content_path)) {
    auto rel_path = ini->get<std::string>("path.rel", "");
    content_path = ConfigDirs::getUserDirectory() / rel_path;
  }
  fs::path cfg_ini = content_path / "config.ini";

  if (!sys->pathExists(cfg_ini) || !sys->pathCanRead(cfg_ini)) {
    return absl::NotFoundError(absl::StrFormat(
        "Unable to parse %s, no access to config: %s", name, cfg_ini.string()));
  }

  auto config = std::make_unique<IniFile>(cfg_ini);
  if (!config->read()) {
    return absl::InternalError("Unable to parse ini file: " + cfg_ini.string());
  }
  return Avd(content_path, std::move(ini), std::move(config), name);
}

// Check that an AVD name is valid.
static bool _checkAvdName(const std::string &name) {
  int len = strspn(name.c_str(), "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                 "abcdefghijklmnopqrstuvwxyz"
                                 "0123456789_.-");
  return (name.size() == len);
}

std::vector<std::string> Avd::list() {
  std::vector<std::string> avds;
  auto pattern = std::regex(".*.ini");
  auto directory_path = ConfigDirs::getAvdRootDirectory();

  for (const auto &entry : fs::directory_iterator(directory_path)) {
    const auto &filename = entry.path().filename().string();

    // Simple pattern matching
    if (std::regex_match(filename, pattern)) {
      std::string name = filename;
      name.erase(name.size() - 4);
      if (_checkAvdName(name)) {
        avds.push_back(name);
      }
    }
  }
  return avds;
}

} // namespace android::goldfish