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
#include <memory>
#include <string>
#include <vector>

#include "android/goldfish/device.h"

// Configures the "motherboard", it will determine the qemu binary needed to run
// and will also setup the proper motherboard configuration, including magical
// goldfish acpi parameters.
namespace android::goldfish {

class Machine : public Device {
  public:
    explicit Machine() : Device("machine") {}

    fs::path qemu_binary() { return mBinary; }
    absl::Status initialize(const EmulatorConfig& emulator) override;
    std::vector<std::string> getQemuParameters(const EmulatorConfig& emulator) const override;

  private:
    fs::path mBinary;
    std::string mMachine;
};
}  // namespace android::goldfish