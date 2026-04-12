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
#include <string>
#include <vector>

#include "device.h"

namespace android::goldfish {

// Configures the rutabaga gfxstream based graphics card.
// The gpu card lives in the first pci slot (01.0)
class GpuDevice : public PciDevice {
  public:
    explicit GpuDevice(std::string_view gpu_name) : PciDevice("gpu", "01.0"), mGpuName(gpu_name) {}

    absl::Status initialize(const EmulatorConfig& emulator) override;
    std::vector<std::string> getQemuParameters(const EmulatorConfig& emulator) const override;

  private:
    std::string mGpuName;
};

}  // namespace android::goldfish
