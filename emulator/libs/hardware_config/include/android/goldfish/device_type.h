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

#include <cstdint>

namespace android::goldfish {

enum class DeviceType : uint8_t {
    kPhone = 0,
    kTv = 1,
    kWear = 2,
    kAndroidAuto = 3,
    kDesktop = 4,
    kUnknown = 255,
};

}  // namespace android::goldfish