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
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"

#include "android/goldfish/device_type.h"
#include "android/goldfish/hardware_config.h"
#include "android/goldfish/image_list.h"
#include "android/goldfish/ini_file.h"
#include "android/goldfish/input_paths.h"
#include "goldfish/metrics/studio_stats_wrapper.h"

namespace android::goldfish {
namespace fs = std::filesystem;

/**
 * @brief Models an Android Virtual Device (AVD), providing a representation
 *        of its configuration, content, and hardware characteristics.
 *
 * AVDS encapsulate the essential components and settings required to simulate
 * an Android device. Key elements of an AVD include:
 *
 *  * **Content Directory:** Contains virtual disk images (system, data, etc.)
 *     and device-specific configuration files.
 *
 *  * **Root Configuration File (`*.ini`)**: Contains metadata, including a
 *     `rootPath` property specifying the content directory's location. May
 *      cache additional device properties for optimization.
 *
 *  * **Hardware Configuration:** Represents the device's virtual hardware
 *     specifications, potentially including CPU, memory, screen, and other
 *     device features.
 *
 * Note: The AVD's content directory can be relocated by updating
 *    the `rootPath` in its configuration file.
 */
class Avd {
  public:
    static constexpr int kUnknownApiLevel = 1000;

    // NOLINTBEGIN
/* define the enumared values corresponding to each AVD image type
 * examples are: KERNEL, SYSTEM, etc..
 */
#define _AVD_IMG(x, y, z) x,
    enum class ImageType : uint8_t {
        AVD_IMAGE_LIST AVD_IMAGE_MAX /* do not remove */
    };
#undef _AVD_IMG
    // NOLINTEND

    enum class CpuArchitecture : uint8_t {
        kX86 = 0,
        kArm,
        kRiscV,
        kUnknown,
    };

    virtual ~Avd() = default;

    // A detailed string describing this avd
    virtual std::string Details(bool verbose) const = 0;

    /**
     * @brief Returns the human-readable name of the AVD. This name corresponds to
     * the filename of the AVD's configuration file (without the ".ini"
     * extension).
     */
    virtual std::string Name() const = 0;

    virtual std::string Id() const = 0;
    virtual CpuArchitecture Arch() const = 0;
    virtual std::string Abi() const = 0;
    virtual std::string BuildSdk() const = 0;
    virtual std::string BuildId() const = 0;
    virtual std::string BuildFingerprint() const = 0;
    virtual int64_t BuildTimestamp() const = 0;
    virtual std::string BuildFlavour() const = 0;
    virtual std::string BuildProductName() const = 0;
    virtual std::string BuildNumber() const = 0;

    // Type of the device this will be extracted for the build.prop
    // file associated with the system image used by this avd.
    virtual DeviceType GetDeviceType() const = 0;

    /**
     * @brief Returns the path to the AVD's content directory. This is typically
     * ~/.android/avd/<name()>.
     */
    virtual fs::path GetContentPath() const = 0;

    /**
     * @brief Retrieves the file path of a system image associated with an Android
     * emulator.
     *
     * This method retrieves the file path of the specified system image type
     * associated with an Android emulator. The system image directory is located
     * under the $ANDROID_SDK_ROOT/ directory as specified
     * by the image.sysdir.1 or image.sysdir.2 property in the config.ini file
     *
     * @param img_type The type of system image to retrieve.
     * @return A StatusOr object containing the file path of the system image on
     * success, or an error status on failure.
     *
     * @see Avd::ImageType
     * @see ConfigDirs::getSdkRootDirectory
     */
    virtual const SystemImagePaths& GetSystemImagePaths() const = 0;

    virtual const HardwareConfig& Hw() const = 0;

    virtual android_studio::EmulatorAvdInfo::EmulatorAvdImageKind ImageKind() const = 0;

    /**
     * @brief Retrieves the API level of the AVD.
     *
     * This method extracts the API level from the `target` property in the AVD's
     * configuration file. The `target` property can have two formats:
     *  *  `android-<level>`
     *  *  `<vendor-name>:<add-on-name>:<level>`
     *
     * The `<level>` can be a decimal number or a code name
     * (e.g., "Tiramisu").
     *
     * @return The API level as an integer. Returns `kUnknownApiLevel` if the
     *         API level cannot be determined.
     */
    virtual int ApiLevel() const = 0;

    /**
     * @brief Retrieves the dessert name associated with the AVD's API level.
     *
     * This method returns the dessert name (e.g., "Tiramisu") corresponding to the
     * AVD's API level.
     *
     * @return The dessert name as a string, or an empty string if the API level
     *         does not have a corresponding dessert name.
     */
    virtual std::string Dessert() const = 0;

    /**
     * @brief Retrieves a descriptive string for the AVD's API level.
     *
     * This method returns a string that describes the API level, including the
     * version number, code name (if applicable), and potentially other relevant
     * information.
     *
     * @return The API description string.
     */
    virtual std::string ApiDescription() const = 0;

    /**
     * @brief Returns the AVD's display name if set, otherwise the name.
     *
     * @return The displayname if set, otherwise the name.
     *         is not found.
     */
    virtual std::string DisplayName() const = 0;

    virtual std::string SkinName() const = 0;

    virtual int ForcedTrampolineVersion() const = 0;

    /**
     * @brief Returns the qemu version of the emulator that ran this AVD.
     * @return An absl::StatusOr<std::optional<int>> object. On success,
     *         contains the version value for the last run. It'll be nullopt
     *.        if the AVD has not been run before or if it's data is wiped.
     */
    virtual absl::StatusOr<std::optional<int>> GetLastRunQemuVersion() const = 0;

    /**
     * @brief Sets the qemu version of the emulator that ran this AVD.
     * @return absl::Status indicating success or failure.
     */
    virtual absl::Status SetLastRunQemuVersion(int version) = 0;

    /**
     * @brief Retrieves the filename associated with the given AVD image type.
     *
     * @param img_type The AVD image type for which to retrieve the filename.
     * @return fs::path The corresponding image filename.
     */
    static fs::path GetImageFilename(Avd::ImageType img_type);

    /**
     * @brief Lists the names of available Android Virtual Devices (AVDs).
     *
     * This method scans the standard location where AVDs are stored, looking for
     * .ini configuration files. The names of the AVDs are based on the names of
     * these files.
     *
     * @return A std::vector containing the names of discovered AVDs.
     */
    static std::vector<std::string> List(const fs::path& avd_directory);

    /**
     * @brief Constructs an AVD object from its name.
     *
     * @param name The name of the AVD.
     * @param sysdir_override Optionally supply a path to override the system directory
     *        search. Use empty string for default behaviour.
     * @param writable_content_override Optionally supply a path to override the content
     *        directory. Use empty string for default behaviour.
     * @return An absl::StatusOr<Avd> object. On success, contains the
     *         constructed AVD. On failure, contains an error status.
     */
    static absl::StatusOr<std::unique_ptr<Avd>> FromName(const AndroidOptions& opts,
                                                         const android::goldfish::UserPaths& paths,
                                                         const std::string& name, bool wipe_data,
                                                         fs::path writable_content_override,
                                                         fs::path sysdir_override = {});

    static absl::StatusOr<std::unique_ptr<Avd>> FromAndroidBuild(
            const AndroidOptions& opts, const android::goldfish::UserPaths& user_paths,
            const std::string& name, fs::path android_build_out, bool wipe_data,
            fs::path writable_content_override);

  private:
    static absl::StatusOr<std::unique_ptr<Avd>> FromSysDirs(
            const AndroidOptions& opts, const android::goldfish::UserPaths& user_paths,
            const std::string& name, IniFile config_ini, fs::path content_dir,
            SystemImagePaths system_image_paths);
};

}  // namespace android::goldfish
