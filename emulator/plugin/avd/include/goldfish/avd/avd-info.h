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

#include "android/goldfish/config/hardware_config.h"
#include "goldfish/devices/connector_registry.h"

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
extern "C" {
#include "hw/qdev-core.h"
#include "qom/object.h"
#include "qapi/error.h"
}

#ifdef QEMU_OS_WIN32_H
#undef listen
#undef send
#undef connect
#endif
// IWYU pragma: end_keep
// clang-format on

struct AvdInfoDev {
    DeviceClass parent_class;
    int32_t serial_number{0};
    int32_t adb_port{0};
    std::string avd_name;
    std::string avd_id;
    std::string avd_abi;
    int avd_api;
    std::filesystem::path avd_content_path;
    std::string build_sdk;
    std::string build_id;
    std::string build_flavour;
    int32_t quit_after_boot_timeout_seconds{0};
};

#define TYPE_AVD "avdstart"
#define AVD_INFO_DEV(obj) OBJECT_CHECK(AvdInfoDev, (obj), TYPE_AVD)
#define AVD_INFO_DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(AvdInfoDev, obj, TYPE_AVD)

template <typename Sink>
void AbslStringify(Sink& sink, AvdInfoDev dev) {
    absl::Format(&sink,
                 "AvdInfoDev: name={%s}, parent_class.fw_name={%s}",
                 dev.avd_name, dev.parent_class.fw_name);
}

namespace goldfish::avd_info {

struct AvdProperties {
    // TODO devices should probably just lookup the AvdInfoDev using qemu object calls.
    const AvdInfoDev* avd_info;

    android::goldfish::HardwareConfig hw_config;
};

const AvdProperties *get_avd();

devices::ConnectorRegistry& connector_registry();

void avd_info_register_types(void);

}  // namespace goldfish::avd_info
