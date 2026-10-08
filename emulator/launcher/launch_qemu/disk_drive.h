
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
#include <string>
#include <vector>

#include "absl/status/status.h"

#include "device.h"

namespace android::goldfish {

namespace fs = std::filesystem;

// A dummy drive backed by QEMU's null-co driver.
class NullDrive : public PciDevice {
  public:
    explicit NullDrive(std::string id, std::string addr)
            : PciDevice(std::move(id), std::move(addr)) {}

    absl::Status initialize(const EmulatorConfig& emulator) override { return absl::OkStatus(); }
    std::vector<std::string> getQemuParameters(const EmulatorConfig& emulator) const override;
};

// A raw qemu.img that is read only.
class RoDrive : public PciDevice {
  public:
    explicit RoDrive(std::string id, std::string addr, fs::path image_path)
            : PciDevice(id, addr), mImagePath(image_path) {}

    absl::Status initialize(const EmulatorConfig& emulator) override;
    std::vector<std::string> getQemuParameters(const EmulatorConfig& emulator) const override;

    // for testing
    const fs::path& image_path() const { return mImagePath; }

  protected:
    fs::path mImagePath;
};

/**
 * @brief Represents a mutable disk (qcow2 backed).
 */
class Qcow2Drive : public PciDevice {
  public:
    std::vector<std::string> getQemuParameters(const EmulatorConfig& emulator) const override;

    // for testing
    const fs::path& backing_image() const { return backing_image_; }
    const fs::path& qcow2_image() const { return qcow2_image_; }

  protected:
    Qcow2Drive(std::string id, std::string addr, fs::path backing_image, fs::path qcow2_image)
            : PciDevice(std::move(id), std::move(addr))
            , backing_image_(std::move(backing_image))
            , qcow2_image_(std::move(qcow2_image)) {}

    absl::Status MakeQcow2(const EmulatorConfig& emulator) const;

    const fs::path backing_image_;
    const fs::path qcow2_image_;
};

/**
 * @brief Represents a mutable disk created from an already existing image pointed by `img_path`.
 */
class WritableImgDrive : public Qcow2Drive {
  public:
    WritableImgDrive(std::string id, std::string addr, fs::path backing_image, fs::path qcow2_image)
            : Qcow2Drive(std::move(id), std::move(addr), std::move(backing_image),
                         std::move(qcow2_image)) {}

    absl::Status initialize(const EmulatorConfig& emulator) override;
};

/**
 * @brief Represents a mutable disk of `size_bytes` size. `img_path` must be located
 * inside the AVD folder.
 */
class BlankWritableDrive : public Qcow2Drive {
  public:
    BlankWritableDrive(std::string id, std::string addr, fs::path backing_image,
                       fs::path qcow2_image, uint64_t size_bytes)
            : Qcow2Drive(std::move(id), std::move(addr), std::move(backing_image),
                         std::move(qcow2_image))
            , size_bytes_(size_bytes) {}

    absl::Status initialize(const EmulatorConfig& emulator) override;

  protected:
    const uint64_t size_bytes_;
};

/**
 * @brief Represents a mutable disk created from `src_directory` of size `size_bytes`.
 * `img_path` must be located inside the AVD folder.
 */
class WritableDirectoryDrive : public Qcow2Drive {
  public:
    WritableDirectoryDrive(std::string id, std::string addr, fs::path backing_image,
                           fs::path qcow2_image, fs::path src_directory, uint64_t size_bytes)
            : Qcow2Drive(std::move(id), std::move(addr), std::move(backing_image),
                         std::move(qcow2_image))
            , src_directory_(std::move(src_directory))
            , size_bytes_(size_bytes) {}

    absl::Status initialize(const EmulatorConfig& emulator) override;

    // for testing
    const fs::path& src_directory() const { return src_directory_; }

  protected:
    const fs::path src_directory_;
    const uint64_t size_bytes_;
};

}  // namespace android::goldfish
