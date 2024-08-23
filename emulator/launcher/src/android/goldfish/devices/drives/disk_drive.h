
// Copyright (C) 2024 The Android Open Source Project
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
#include <memory>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"

#include "android/base/system/storage_capacity.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/hardware_config.h"
#include "android/goldfish/devices/device.h"

namespace android::goldfish {

namespace fs = std::filesystem;

// A raw qemu.img that is read only.
class RawDrive : public PciDevice {

public:
  explicit RawDrive(std::string id, std::string addr, Avd::ImageType image)
      : PciDevice(id, addr), mImage(image) {}
  ~RawDrive() override = default;

  absl::Status initialize(const Emulator& emulator) override;
  std::vector<std::string> getQemuParameters(const Emulator& emulator) const override;

protected:
  Avd::ImageType mImage;
};

/**
 * @brief Represents a mutable disk drive backed by a Qcow2 image, connected to the PCI bus.
 */
class MutableDiskDrive : public PciDevice {
public:
  /**
   * @brief Constructor.
   * @param id Unique ID of the disk drive device.
   * @param addr PCI slot and optional function number (e.g., "06.0")
   */
  explicit MutableDiskDrive(std::string id, std::string addr = "")
      : PciDevice(id, addr) {}

  /**
   * @brief Overrides Device::getQemuParameters() to provide QEMU configuration.
   * @param emulator Pointer to the parent emulator.
   * @return A vector of QEMU configuration strings for this disk drive.
   */
  std::vector<std::string> getQemuParameters(const Emulator& emulator) const override;

  /**
   * @brief Overrides Device::clear() to erase the disk image (if it exists).
   */
  void clear() override;

  /**
   * @brief Checks if the underlying disk image file exists.
   * @return True if the disk image file exists, false otherwise.
   */
  bool exists() { return fs::exists(mDiskImage); }


  /**
   * @brief Resizes a partition within the disk image.
   * @param partition Path to the partition within the disk image.
   * @param size The desired new size of the partition. Must be between 128 MiB and 16 TiB.
   * @return absl::Status indicating success or failure. Note: Underlying resize2fs tool may still fail with its own error codes.
   */
  absl::Status resizePartition(fs::path partition, StorageCapacity size);

  /**
   * @brief Converts a disk image to Qcow2 format using the qemu-img utility.
   * @param image Path to the disk image to convert.
   * @return absl::Status indicating success or failure.
   */
  absl::Status convertImgToQcow2(fs::path image);

  /**
   * @brief Creates a new empty ext4 disk image.
   * @param image Path to create the new disk image.
   * @param size Size of the disk image.
   * @param mount_point Mount point associated with the disk image.
   * @return absl::Status indicating success or failure.
   */
  absl::Status createExt4Image(fs::path image, StorageCapacity size,
                               std::string mount_point);

  /**
   * @brief Creates an ext4 image from a directory, copying its contents.
   * @param source_directory Path to the source directory to copy.
   * @param image Path to create the new disk image.
   * @param size Size of the disk image.
   * @param mount_point Mount point associated with the disk image.
   * @return absl::Status indicating success or failure.
   */
  absl::Status createExt4ImageFromDirectory(fs::path source_directory,
                                            fs::path image,
                                            StorageCapacity size,
                                            std::string mount_point);

protected:
  fs::path mDiskImage;
  std::string mDiskId;
};

} // namespace android::goldfish