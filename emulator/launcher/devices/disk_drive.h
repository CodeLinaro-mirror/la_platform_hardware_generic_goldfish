
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

// A raw qemu.img that is read only.
class RoDrive : public PciDevice {
  public:
    explicit RoDrive(std::string id, std::string addr, fs::path image_path)
            : PciDevice(id, addr), mImagePath(image_path) {}

    absl::Status initialize(const EmulatorConfig& emulator) override;
    std::vector<std::string> getQemuParameters(const EmulatorConfig& emulator) const override;

  protected:
    fs::path mImagePath;
};

/**
 * @brief Represents a mutable disk drive backed by a Qcow2 image, connected to
 * the PCI bus.
 */
class RwDrive : public PciDevice {
  public:
    explicit RwDrive(std::string id, std::string addr, std::optional<fs::path> src_path,
                     fs::path dst_image, fs::path qcow2_image, uint64_t size_bytes)
            : PciDevice(id, addr)
            , mSourcePath(src_path)
            , mDestinationImage(dst_image)
            , mQcow2Image(qcow2_image)
            , mSizeBytes(size_bytes) {}

    absl::Status initialize(const EmulatorConfig& emulator) override;
    std::vector<std::string> getQemuParameters(const EmulatorConfig& emulator) const override;

  protected:
    std::optional<fs::path> mSourcePath;
    fs::path mDestinationImage;
    fs::path mQcow2Image;
    uint64_t mSizeBytes;
};

}  // namespace android::goldfish
