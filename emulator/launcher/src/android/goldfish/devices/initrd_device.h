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
#include <filesystem>
#include <string>
#include <vector>

#include "android/goldfish/device.h"

namespace android::goldfish {

// Exposed for testing.
std::vector<std::pair<std::string, std::string>> getBootProperties(const EmulatorConfig& emulator);

/**
 * @brief Represents the initial RAM disk (initrd) used by the kernel.
 *
 * This class manages configuring the initial ramdisk and providing the
 * configuration properties necessary for the emulator to boot.
 */
class InitrdDevice : public Device {
  public:
    explicit InitrdDevice() : Device("initrd") {}

    absl::Status initialize(const EmulatorConfig& emulator) override;
    std::vector<std::string> getQemuParameters(const EmulatorConfig& emulator) const override;

  private:
    fs::path mUserRamdisk;
};

}  // namespace android::goldfish