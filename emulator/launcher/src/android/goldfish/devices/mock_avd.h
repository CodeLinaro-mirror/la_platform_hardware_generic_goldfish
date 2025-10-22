// Copyright 2025 The Android Open Source Project
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
#include <gmock/gmock.h>

#include <filesystem>
#include <string>

#include "absl/status/statusor.h"

#include "android/goldfish/config/avd.h"
#include "android/goldfish/config/hardware_config.h"

namespace android::goldfish {
namespace fs = std::filesystem;

class MockAvd : public Avd {
  public:
    MOCK_METHOD(std::string, details, (bool verbose), (const override));
    MOCK_METHOD(std::string, name, (), (const override));
    MOCK_METHOD(Avd::DeviceType, getDeviceType, (), (const override));
    MOCK_METHOD(fs::path, getContentPath, (), (const override));
    MOCK_METHOD(absl::StatusOr<fs::path>, getImageFilePath, (Avd::ImageType imgType),
                (const override));
    MOCK_METHOD(absl::StatusOr<fs::path>, getSystemImageFilePath, (Avd::ImageType imgType),
                (const override));
    MOCK_METHOD(bool, hasEncryptionKey, (), (const override));
    MOCK_METHOD(CpuArchitecture, detectArchitecture, (), (const override));

    MOCK_METHOD(fs::path, getSdkPath, (), (const override));
    MOCK_METHOD(fs::path, getAvdPath, (), (const override));

    MOCK_METHOD(const HardwareConfig&, hw, (), (const override));
    MOCK_METHOD(bool, playstore, (), (const override));
    MOCK_METHOD(int, apiLevel, (), (const override));
    MOCK_METHOD(std::string, dessert, (), (const override));
    MOCK_METHOD(std::string, apiDescription, (), (const override));
    MOCK_METHOD(fs::path, getConfigIniPath, (), (const override));
    MOCK_METHOD(std::string, display_name, (), (const override));
};

}  // namespace android::goldfish
