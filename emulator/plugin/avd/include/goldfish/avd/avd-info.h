// Copyright 2024 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#pragma once

#include <filesystem>
#include <string>

#include "android/goldfish/config/device_type.h"
#include "android/goldfish/config/hardware_config.h"
#include "goldfish/devices/connector_registry.h"

namespace goldfish::avd_info {

struct AvdProperties {
    int32_t serial_number{0};
    int32_t adb_port{0};
    std::string avd_name;
    std::string avd_id;
    std::string avd_abi;
    int avd_api{0};
    android::goldfish::DeviceType avd_type{android::goldfish::DeviceType::kUnknown};
    std::filesystem::path avd_content_path;
    std::string build_sdk;
    std::string build_id;
    std::string build_flavour;
    int32_t quit_after_boot_timeout_seconds{0};

    android::goldfish::HardwareConfig hw_config;
};

const AvdProperties *get_avd();

devices::ConnectorRegistry& connector_registry();

void avd_info_register_types(void);

}  // namespace goldfish::avd_info
