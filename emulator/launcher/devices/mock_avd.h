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

#include "android/goldfish/avd.h"
#include "android/goldfish/hardware_config.h"

namespace android::goldfish {
namespace fs = std::filesystem;

class MockAvd : public Avd {
  public:
    MOCK_METHOD(std::string, Details, (bool verbose), (const override));
    MOCK_METHOD(std::string, Name, (), (const override));
    MOCK_METHOD(DeviceType, GetDeviceType, (), (const override));
    MOCK_METHOD(fs::path, GetContentPath, (), (const override));
    MOCK_METHOD(absl::StatusOr<fs::path>, getImageFilePath, (Avd::ImageType img_type),
                (const override));
    MOCK_METHOD(absl::StatusOr<fs::path>, GetSystemImageFilePath, (Avd::ImageType img_type),
                (const override));
    MOCK_METHOD(bool, hasEncryptionKey, (), (const override));
    MOCK_METHOD(CpuArchitecture, DetectArchitecture, (), (const override));

    MOCK_METHOD(fs::path, GetSdkPath, (), (const override));
    MOCK_METHOD(fs::path, GetAvdPath, (), (const override));

    MOCK_METHOD(const HardwareConfig&, Hw, (), (const override));
    MOCK_METHOD(bool, Playstore, (), (const override));
    MOCK_METHOD(int, ApiLevel, (), (const override));
    MOCK_METHOD(std::string, Dessert, (), (const override));
    MOCK_METHOD(std::string, ApiDescription, (), (const override));
    MOCK_METHOD(fs::path, GetConfigIniPath, (), (const override));
    MOCK_METHOD(std::string, DisplayName, (), (const override));
    MOCK_METHOD(std::string, SkinName, (), (const override));
    MOCK_METHOD(absl::Status, Finalize, (), (override));
    MOCK_METHOD(std::string, Id, (), (const override));
    MOCK_METHOD(std::string, Abi, (), (const override));
    MOCK_METHOD(std::string, BuildSdk, (), (const override));
    MOCK_METHOD(std::string, BuildId, (), (const override));
    MOCK_METHOD(std::string, BuildFingerprint, (), (const override));
    MOCK_METHOD(int64_t, BuildTimestamp, (), (const override));
    MOCK_METHOD(std::string, BuildFlavour, (), (const override));
    MOCK_METHOD(std::string, BuildProductName, (), (const override));
};

}  // namespace android::goldfish
