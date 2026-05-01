// Copyright 2026 The Android Open Source Project
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
#include <optional>

#include "absl/status/status.h"

#include "android/goldfish/device_type.h"
#include "android/goldfish/hardware_config.h"

namespace android::goldfish {

/**
 * @brief Utilities for calculating and enforcing RAM size requirements.
 */
class MemoryConfig {
  public:
    /**
     * @brief Calculates the finalized RAM and VM heap size based on hardware config and API level.
     * Updates the provided HardwareConfig with the final values.
     *
     * @param hw The hardware configuration to update.
     * @param api_level The API level of the AVD.
     * @return absl::Status indicating success or failure.
     */
    static absl::Status FinalizeRamAndHeapSize(HardwareConfig& hw, int api_level);

    /**
     * @brief Calculates the minimum RAM size based on hardware config, API level, and device type.
     *
     * @param hw The hardware configuration.
     * @param api_level The API level of the AVD.
     * @param device_type The optional device type of the AVD.
     * @return The minimum RAM size in megabytes.
     */
    static int CalculateMinimumRam(const HardwareConfig& hw, int api_level,
                                   std::optional<DeviceType> device_type = std::nullopt);
};

}  // namespace android::goldfish
