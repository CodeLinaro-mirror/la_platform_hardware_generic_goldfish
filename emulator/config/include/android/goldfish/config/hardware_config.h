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
#include <string>

#include "absl/status/status.h"

#include "aemu/base/files/IniFile.h"
#include "android/base/system/storage_capacity.h"
namespace android::goldfish {

using base::StorageCapacity;
using base::operator""_MiB;
class Avd;

// describes the properties of a given virtual device configuration file.
class HardwareConfig {
  public:
    HardwareConfig();
    ~HardwareConfig() = default;

    void applyDefaults(const Avd &avd);
    void load(IniFile* ini);

#define HWCFG_BOOL(n, s, d, a, t) bool n;
#define HWCFG_INT(n, s, d, a, t) int n;
#define HWCFG_STRING(n, s, d, a, t) std::string n;
#define HWCFG_DOUBLE(n, s, d, a, t) double n;
#define HWCFG_DISKSIZE(n, s, d, a, t) StorageCapacity n;

#include "host-common/hw-config-defs.h"
    StorageCapacity hw_sdCard_size{512_MiB};
};

}  // namespace android::goldfish