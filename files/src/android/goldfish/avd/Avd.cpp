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
#include "android/goldfish/avd/Avd.h"

#include <filesystem>
#include <memory>
#include <regex>
#include <unordered_map>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "android/base/system/System.h"
#include "android/goldfish/ConfigDirs.h"
#include "android/goldfish/IniFile.h"
#include "android/goldfish/avd/keys.h"

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
using AvdFlavor = android::goldfish::Avd::Flavor;

static const std::string_view
    _imageFileNames[static_cast<int>(AvdImageType::AVD_IMAGE_MAX)] = {
#define _AVD_IMG(x, y, z) y,
        AVD_IMAGE_LIST
#undef _AVD_IMG
};

static std::string getIconForFlavor(AvdFlavor flavor) {
  switch (flavor) {
  case AvdFlavor::PHONE:
    return "📱"; // 📱 (Smartphone)
  case AvdFlavor::TV:
    return "📺"; // 📺 (Television)
  case AvdFlavor::WEAR:
    return "⌚️"; // ⌚️ (Smartwatch)
  case AvdFlavor::ANDROID_AUTO:
    return "🚗"; // 🚗 (Car)
  case AvdFlavor::DESKTOP:
    return "🖥️"; // 🖥️ (Desktop computer)
  default:
    return "🤷"; // 🤷 (Unknown)
  }
}

absl::StatusOr<Avd> Avd::fromName(std::string name) {
  auto directory_path = ConfigDirs::getAvdRootDirectory();
  return Avd::parse(directory_path / (name + ".ini"), name);
}

AvdFlavor Avd::getFlavor() {
  if (mFlavor.has_value()) {
    return mFlavor.value();
  }

  AvdFlavor res = AvdFlavor::OTHER;

  const std::unordered_map<std::string, Flavor> labelMap{
      {"phone", Flavor::PHONE},      {"atv", Flavor::TV},
      {"wear", Flavor::WEAR},        {"aw", Flavor::WEAR},
      {"car", Flavor::ANDROID_AUTO}, {"pc", Flavor::DESKTOP}};

  const PropertyList props = {"ro.product.name", "ro.product.system.name",
                              "ro.build.flavor"};

  auto sysimg = getSystemImagePath(AvdImageType::BUILDPROP);
  if (!sysimg.ok()) {
    dwarning("Unable to retrieve image path: %s, using unknown avd flavor",
             sysimg.status().message());
    return AvdFlavor::OTHER;
  }

  auto buildprop = sysimg.value() / "build.prop";
  if (!System::get()->pathExists(buildprop) ||
      !System::get()->pathCanRead(buildprop)) {
    dwarning("Unable to read build properties: %s, using unknown avd flavor",
             buildprop);
    return AvdFlavor::OTHER;
  }
  IniFile buildIni(buildprop);
  buildIni.read();

  for (const auto &prop : props) {
    if (!buildIni.hasKey(prop)) {
      continue;
    }

    auto build = buildIni.getString(prop, "_unused");
    for (const auto &[key, val] : labelMap) {
      if (build.find(key) != std::string::npos) {
        // We found the flavor;
        dinfo("Found %s in %s", key, build);
        mFlavor = val;
        return mFlavor.value();
      }
    }
  }

  // Likely unknown.
  return res;
}

absl::StatusOr<fs::path> Avd::getImagePath(AvdImageType imgType) {
  auto possible = mContentPath / _imageFileNames[static_cast<uint8_t>(imgType)];
  if (System::get()->pathIsFile(possible) &&
      System::get()->pathCanRead(possible)) {
    return possible;
  }
  dinfo("Did not find %s in %s, falling back to system path", possible, mContentPath);
  return getSystemImagePath(imgType);
}

absl::StatusOr<fs::path> Avd::getSystemImagePath(AvdImageType imgType) {
  auto sdk = ConfigDirs::getSdkRootDirectory();
  fs::path path = "no-sysimg";
  for (int n = 0; n < MAX_SEARCH_PATHS; n++) {
    std::string key = absl::StrFormat("%s%d", SEARCH_PREFIX, n);
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
      absl::StrFormat("Path %s does not exist (%s) in %s", path, SEARCH_PREFIX,
                      mConfig->getBackingFile()));
}

std::string Avd::details() {
  auto display = mConfig->getString("tag.display", "");
  auto width = mConfig->getString("hw.lcd.width", "?");
  auto height = mConfig->getString("hw.lcd.height", "?");
  auto icon = getIconForFlavor(getFlavor());
  return absl::StrFormat("%s  - (%sx%s) %s %s", mName, width, height, display,
                         icon);
}

Avd::Avd(fs::path content_path, std::unique_ptr<IniFile> target,
         std::unique_ptr<IniFile> config, std::string name)
    : mContentPath(content_path), mTarget(std::move(target)),
      mConfig(std::move(config)), mName(name) {}

absl::StatusOr<Avd> Avd::parse(fs::path target, std::string name) {
  auto sys = System::get();
  if (!sys->pathExists(target) || !sys->pathCanRead(target)) {
    return absl::NotFoundError("Unable to parse " + name +
                               ", no access to: " + target.string());
  }

  auto avd_ini = target;
  auto ini = std::make_unique<IniFile>(avd_ini);
  if (!ini->read()) {
    return absl::InternalError("Unable to parse ini file: " + avd_ini.string());
  }

  auto rel_path = ini->getString("path.rel", "avd/" + name + ".avd");
  auto content_path = ConfigDirs::getUserDirectory() / rel_path;
  auto cfg_ini = content_path / "config.ini";
  if (!sys->pathExists(cfg_ini) || !sys->pathCanRead(cfg_ini)) {
    return absl::NotFoundError("Unable to parse " + name +
                               ", no access to config: " + cfg_ini.string());
  }

  auto config = std::make_unique<IniFile>(cfg_ini);
  if (!config->read()) {
    return absl::InternalError("Unable to parse ini file: " + cfg_ini.string());
  }
  for (auto it = config->begin(); it != config->end(); ++it) {
    dinfo("  %s = %s", *it, config->getString(*it, "---"));
  }
  dinfo("Using content path: %s", content_path);
  return Avd(content_path, std::move(ini), std::move(config), name);
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
      avds.push_back(name);
    }
  }
  return avds;
}
} // namespace android::goldfish