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

#include "aemu/base/files/IniFile.h"
#include "android/goldfish/config/hardware_config.h"
#include "android/goldfish/config/image_list.h"

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
    enum class DeviceType : uint8_t {
        kPhone = 0,
        kTv = 1,
        kWear = 2,
        kAndroidAuto = 3,
        kDesktop = 4,
        kUnknown = 255,
    };

/* define the enumared values corresponding to each AVD image type
 * examples are: KERNEL, SYSTEM, etc..
 */
#define _AVD_IMG(x, y, z) x,
    enum class ImageType : uint8_t {
        AVD_IMAGE_LIST AVD_IMAGE_MAX /* do not remove */
    };
#undef _AVD_IMG

    enum class CpuArchitecture : uint8_t {
        kX86 = 0,
        kArm,
        kRiscV,
        kUnknown,
    };

    virtual ~Avd() = default;

    // A detailed string describing this avd
    virtual std::string details(bool verbose) const = 0;

    /**
     * @brief Returns the human-readable name of the AVD. This name corresponds to
     * the filename of the AVD's configuration file (without the ".ini"
     * extension).
     */
    virtual std::string name() const = 0;

    // Type of the device this will be extracted for the build.prop
    // file associated with the system image used by this avd.
    virtual DeviceType getDeviceType() const = 0;

    /**
     * @brief Returns the path to the AVD's content directory. This is typically
     * ~/.android/avd/<name()>.
     */
    virtual fs::path getContentPath() const = 0;

    /**
     * @brief Retrieves an AVD image file path, favoring the content directory.
     *
     * Attempts to locate the image file within the AVD's content directory.
     * If not found, searches for the system image based on configuration
     * settings.
     *
     * @param imgType The type of AVD image to retrieve.
     * @return A StatusOr object containing the file path on success, or an
     *         error status on failure.
     *
     * @see Avd::ImageType
     * @see getSystemImagePath
     */
    virtual absl::StatusOr<fs::path> getImageFilePath(Avd::ImageType imgType) const = 0;

    /**
     * @brief Retrieves the file path of a system image associated with an Android
     * emulator.
     *
     * This method retrieves the file path of the specified system image type
     * associated with an Android emulator. The system image directory is located
     * under the $ANDROID_SDK_ROOT/ directory as specified
     * by the image.sysdir.1 or image.sysdir.2 property in the config.ini file
     *
     * @param imgType The type of system image to retrieve.
     * @return A StatusOr object containing the file path of the system image on
     * success, or an error status on failure.
     *
     * @see Avd::ImageType
     * @see ConfigDirs::getSdkRootDirectory
     */
    virtual absl::StatusOr<fs::path> getSystemImageFilePath(Avd::ImageType imgType) const = 0;

    /**
     * @brief Checks if the AVD supports encryption.
     *
     * This method determines encryption support by checking if the
     * encryption key image file can be located.
     *
     * @return True if the encryption key image is found, false otherwise.
     */
    virtual bool hasEncryptionKey() const = 0;

    /**
     * @brief Detects the CPU architecture of the AVD based on the 'abi.type'
     * config value.
     *
     * This method analyzes the 'abi.type' property in the AVD's configuration
     * file. Possible architectures are inferred based on the presence of the
     * following substrings:
     *   * "x86" : Indicates an x86 architecture.
     *   * "arm" : Indicates an ARM architecture.
     *
     * @return The detected CpuArchitecture or kUnknown if it cannot be detected.
     */
    virtual CpuArchitecture detectArchitecture() const = 0;

    virtual const HardwareConfig& hw() const = 0;
    virtual bool playstore() const = 0;

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
    virtual int apiLevel() const = 0;

    /**
     * @brief Retrieves the dessert name associated with the AVD's API level.
     *
     * This method returns the dessert name (e.g., "Tiramisu") corresponding to the
     * AVD's API level.
     *
     * @return The dessert name as a string, or an empty string if the API level
     *         does not have a corresponding dessert name.
     */
    virtual std::string dessert() const = 0;

    /**
     * @brief Retrieves a descriptive string for the AVD's API level.
     *
     * This method returns a string that describes the API level, including the
     * version number, code name (if applicable), and potentially other relevant
     * information.
     *
     * @return The API description string.
     */
    virtual std::string apiDescription() const = 0;

    /**
     * @brief Returns the path to the AVD's configuration file.
     *
     * This method returns the path to the AVD's configuration file, which is
     * typically located in ~/.android/avd/
     *
     * @return Path to the avd configuration file
     */
    virtual fs::path getIniFile() const = 0;

    /**
     * @brief Returns the AVD's display name if set, otherwise the name.
     *
     * @return The displayname if set, otherwise the name.
     *         is not found.
     */
    virtual std::string display_name() const = 0;

    /**
     * @brief Retrieves the filename associated with the given AVD image type.
     *
     * @param imgType The AVD image type for which to retrieve the filename.
     * @return fs::path The corresponding image filename.
     */
    static fs::path getImageFilename(Avd::ImageType imgType);

    /**
     * @brief Lists the names of available Android Virtual Devices (AVDs).
     *
     * This method scans the standard location where AVDs are stored, looking for
     * .ini configuration files. The names of the AVDs are based on the names of
     * these files.
     *
     * @return A std::vector containing the names of discovered AVDs.
     */
    static std::vector<std::string> list();

    /**
     * @brief Constructs an AVD object from its name.
     *
     * @param name The name of the AVD.
     * @param sysdir_override Optionally supply a path to override the system directory
     *        search. Use empty string for default behaviour.
     * @return An absl::StatusOr<Avd> object. On success, contains the
     *         constructed AVD. On failure, contains an error status.
     */
    static absl::StatusOr<std::unique_ptr<Avd>> fromName(std::string name,
                                                         std::string sysdir_override = "",
                                                         bool read_only = false);

    static constexpr int kUnknownApiLevel = 1000;

  protected:
    Avd() = default;
};

class FileBackedAvd : public Avd {
  public:
    std::string details(bool verbose) const override;

    std::string name() const override { return mName; }
    DeviceType getDeviceType() const override;
    fs::path getContentPath() const override { return mContentPath; };
    absl::StatusOr<fs::path> getImageFilePath(Avd::ImageType imgType) const override;
    absl::StatusOr<fs::path> getSystemImageFilePath(Avd::ImageType imgType) const override;
    bool hasEncryptionKey() const override;
    CpuArchitecture detectArchitecture() const override;
    const HardwareConfig& hw() const override { return mHwCfg; }
    bool playstore() const override { return false; }
    int apiLevel() const override;
    std::string dessert() const override;
    std::string apiDescription() const override;
    fs::path getIniFile() const override { return mTarget->getBackingFile(); }
    std::string display_name() const override {
        return mConfig->getString("avd.ini.displayname", name());
    }

    static absl::StatusOr<std::unique_ptr<FileBackedAvd>> parse(fs::path ini_file,
                                                                std::string sysdir_override = "",
                                                                bool read_only = false);

   private:
    FileBackedAvd(fs::path content_path, std::unique_ptr<IniFile> target,
                  std::unique_ptr<IniFile> config, std::string name, std::string sysdir_override,
                  bool read_only);

    std::string mName;
    fs::path mContentPath;  // Usually ~/.android/avd/<name>.avd/
    std::unique_ptr<IniFile> mTarget;
    std::unique_ptr<IniFile> mConfig;
    HardwareConfig mHwCfg;
    std::string mSysdirOverride;
};

}  // namespace android::goldfish
