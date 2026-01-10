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
#include <vector>

#include "absl/status/statusor.h"

#include "android/goldfish/image_list.h"
#include "android/goldfish/device_type.h"
#include "android/goldfish/hardware_config.h"
#include "android/goldfish/ini_file.h"
#include "android/goldfish/input_paths.h"

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

    virtual std::string id() const = 0;
    virtual std::string abi() const = 0;
    virtual std::string build_sdk() const = 0;
    virtual std::string build_id() const = 0;
    virtual std::string build_flavour() const = 0;

    // Type of the device this will be extracted for the build.prop
    // file associated with the system image used by this avd.
    virtual DeviceType getDeviceType() const = 0;

    virtual fs::path getSdkPath() const = 0;
    virtual fs::path getAvdPath() const = 0;

    /**
     * @brief Returns the path to the AVD's content directory. This is typically
     * ~/.android/avd/<name()>.
     */
    virtual fs::path getContentPath() const = 0;

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
     * typically ~/.android/avd/<name>.avd/config.ini
     *
     * @return Path to the avd configuration file
     */
    virtual fs::path getConfigIniPath() const = 0;

    /**
     * @brief Returns the AVD's display name if set, otherwise the name.
     *
     * @return The displayname if set, otherwise the name.
     *         is not found.
     */
    virtual std::string display_name() const = 0;

    virtual std::string skin_name() const = 0;
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
    static std::vector<std::string> list(const fs::path& avd_directory);

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
    static absl::StatusOr<std::unique_ptr<Avd>> fromName(
            const android::goldfish::ResolvedInputPaths& paths, std::string name,
            fs::path sysdir_override = {}, fs::path writable_content_override = {});

    static constexpr int kUnknownApiLevel = 1000;

  protected:
    Avd() = default;
};

class FileBackedAvd : public Avd {
  public:
    std::string details(bool verbose) const override;

    fs::path getSdkPath() const override { return mSdkPath; }
    fs::path getAvdPath() const override { return mAvdPath; }

    std::string name() const override { return mName; }
    DeviceType getDeviceType() const override;
    fs::path getContentPath() const override { return mContentPath; };
    absl::StatusOr<fs::path> getSystemImageFilePath(Avd::ImageType imgType) const override;
    CpuArchitecture detectArchitecture() const override;
    const HardwareConfig& hw() const override { return mHwCfg; }
    bool playstore() const override { return false; }
    int apiLevel() const override;
    std::string dessert() const override;
    std::string apiDescription() const override;
    fs::path getConfigIniPath() const override { return mConfig->GetBackingFile(); }
    std::string display_name() const override {
        return mConfig->GetString("avd.ini.displayname", name());
    }
    std::string skin_name() const override { return mConfig->GetString("skin.name", ""); }
    std::string id() const override {
        // TODO allow override with opts.id
        return name();
    }

    std::string abi() const override {
        // TODO check against detected arch.
        return mBuildIni.GetString("ro.product.cpu.abi", "unknown");
    }

    std::string build_sdk() const override {
        return mBuildIni.GetString("ro.build.version.sdk", "unknown");
    }

    std::string build_id() const override { return mBuildIni.GetString("ro.build.id", "unknown"); }

    std::string build_flavour() const override {
        return mBuildIni.GetString("ro.build.flavor", "unknown");
    }

    static absl::StatusOr<std::unique_ptr<FileBackedAvd>> parse(
            std::string name, fs::path config_ini_path, fs::path sdk_path, fs::path avd_path,
            fs::path content_path, fs::path sysdir_override = {});

  private:
    FileBackedAvd(std::string name, std::unique_ptr<IniFile> config, fs::path sdk_path,
                  fs::path avd_path, fs::path content_path, std::vector<fs::path> sys_image_paths);

    bool loadBuildProps();

    std::string mName;
    std::unique_ptr<IniFile> mConfig;
    fs::path mSdkPath;
    fs::path mAvdPath;
    fs::path mContentPath;  // Usually ~/.android/avd/<name>.avd/
    std::vector<fs::path> mSysImagePaths;
    HardwareConfig mHwCfg;
    IniFile mBuildIni;
};

}  // namespace android::goldfish
