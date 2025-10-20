
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
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "android/goldfish/config/avd.h"
#include "android/goldfish/device.h"

namespace android::goldfish {

// A simple list of parameters that need to be added. This is not a real
// "device" Usually this contains things like the telnet server, or -no-reboot,
// etc.
class ParameterList : public Device {
  public:
    explicit ParameterList(std::initializer_list<std::string> params)
        : Device("params_" + std::to_string(++gIdCounter)), mParams(params.begin(), params.end()) {}

    explicit ParameterList(std::vector<std::string> params);
    ~ParameterList() override = default;

    std::vector<std::string> getQemuParameters(const EmulatorConfig& emulator) const override;
    absl::Status initialize(const EmulatorConfig& emulator) override;

  private:
    std::vector<std::string> mParams;
    static int gIdCounter;
};

}  // namespace android::goldfish