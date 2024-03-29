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
#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "android/goldfish/IniFile.h"

namespace android::goldfish {
namespace fs = std::filesystem;

/* An Android Virtual Device (AVD for short) corresponds to a
 * directory containing all kernel/disk images for a given virtual
 * device, as well as information about its hardware capabilities,
 * SDK version number, skin, etc...
 *
 * Each AVD has a human-readable name and is backed by a root
 * configuration file and a content directory. For example, an
 *  AVD named 'foo' will correspond to the following:
 *
 *  - a root configuration file named ~/.android/avd/foo.ini
 *    describing where the AVD's content can be found
 *
 *  - a content directory like ~/.android/avd/foo/ containing all
 *    disk image and configuration files for the virtual device.
 *
 * the 'foo.ini' file should contain at least one line of the form:
 *
 *    rootPath=<content-path>
 *
 * it may also contain other lines that cache stuff found in the
 * content directory, like hardware properties or SDK version number.
 *
 * it is possible to move the content directory by updating the foo.ini
 * file to point to the new location. This can be interesting when your
 * $HOME directory is located on a network share or in a roaming profile
 * (Windows), given that the content directory of a single virtual device
 * can easily use more than 100MB of data.
 *
 */

/* a macro used to define the list of disk images managed by the
 * implementation. This macro will be expanded several times with
 * varying definitions of _AVD_IMG
 */
#define AVD_IMAGE_LIST                                                         \
  _AVD_IMG(KERNEL, "kernel-qemu", "kernel")                                    \
  _AVD_IMG(KERNELRANCHU, "kernel-ranchu", "kernel")                            \
  _AVD_IMG(KERNELRANCHU64, "kernel-ranchu-64", "kernel")                       \
  _AVD_IMG(RAMDISK, "ramdisk.img", "ramdisk")                                  \
  _AVD_IMG(USERRAMDISK, "ramdisk-qemu.img", "user ramdisk")                    \
  _AVD_IMG(INITSYSTEM, "system.img", "init system")                            \
  _AVD_IMG(INITVENDOR, "vendor.img", "init vendor")                            \
  _AVD_IMG(INITDATA, "userdata.img", "init data")                              \
  _AVD_IMG(INITZIP, "data", "init data zip")                                   \
  _AVD_IMG(USERSYSTEM, "system-qemu.img", "user system")                       \
  _AVD_IMG(USERVENDOR, "vendor-qemu.img", "user vendor")                       \
  _AVD_IMG(USERDATA, "userdata-qemu.img", "user data")                         \
  _AVD_IMG(CACHE, "cache.img", "cache")                                        \
  _AVD_IMG(SDCARD, "sdcard.img", "SD Card")                                    \
  _AVD_IMG(ENCRYPTIONKEY, "encryptionkey.img", "Encryption Key")               \
  _AVD_IMG(SNAPSHOTS, "snapshots.img", "snapshots")                            \
  _AVD_IMG(VERIFIEDBOOTPARAMS, "VerifiedBootParams.textproto",                 \
           "Verified Boot Parameters")                                         \
  _AVD_IMG(BUILDPROP, "build.prop", "Build properties")

/* define the enumared values corresponding to each AVD image type
 * examples are: AVD_IMAGE_KERNEL, AVD_IMAGE_SYSTEM, etc..
 */
#define _AVD_IMG(x, y, z) x,
enum class AvdImageType : uint8_t {
  AVD_IMAGE_LIST AVD_IMAGE_MAX /* do not remove */
};
#undef _AVD_IMG

class Avd {
public:
  enum class Flavor : uint8_t {
    PHONE = 0,
    TV = 1,
    WEAR = 2,
    ANDROID_AUTO = 3,
    DESKTOP = 4,
    OTHER = 255,
  };

  ~Avd() = default;
  // Move Constructor
  Avd(Avd &&other) noexcept
      : mContentPath(std::move(other.mContentPath)),
        mTarget(std::move(other.mTarget)), mConfig(std::move(other.mConfig)),
        mName(std::move(other.mName)) {}

  std::string details();
  std::string name() { return mName; }
  fs::path getContentPath() { return mContentPath; };
  absl::StatusOr<fs::path> getImagePath(AvdImageType imgType);
  absl::StatusOr<fs::path> getSystemImagePath(AvdImageType imgType);
  Flavor getFlavor();

  // List all the avd names that are available.
  static std::vector<std::string> list();
  static absl::StatusOr<Avd> fromName(std::string name);

private:
  static absl::StatusOr<Avd> parse(fs::path target, std::string name);
  Avd(fs::path content_path, std::unique_ptr<IniFile> target,
      std::unique_ptr<IniFile> config, std::string name);

  std::unique_ptr<IniFile> mTarget;
  std::unique_ptr<IniFile> mConfig;
  std::string mName;
  std::optional<Flavor> mFlavor;
  fs::path mContentPath;
};

} // namespace android::goldfish