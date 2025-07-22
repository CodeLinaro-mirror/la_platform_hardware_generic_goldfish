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
#include <string>

#include "android/goldfish/config/avd.h"

// clang-format off
// IWYU pragma: begin_keep

extern "C" {
#include "qemu/osdep.h"
#include "hw/qdev-core.h"
#include "qom/object.h"
#include "qapi/error.h"
#include <stdlib.h>
#include <string.h>
}


#ifdef QEMU_OS_WIN32_H
#undef listen
#endif
#include "goldfish/devices/connector_registry.h"

// IWYU pragma: end_keep
// clang-format on

struct AvdInfoDev {
    DeviceClass parent_class;
    std::string ini_path;
    int log_level{2};     // Log only errors
    std::string vmodule;  // Vlog filter
};
#define TYPE_AVD "avdstart"
#define AVD_INFO_DEV(obj) OBJECT_CHECK(AvdInfoDev, (obj), TYPE_AVD)
#define AVD_INFO_DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(AvdInfoDev, obj, TYPE_AVD)

template <typename Sink>
void AbslStringify(Sink& sink, AvdInfoDev dev) {
    absl::Format(&sink,
                 "AvdInfoDev: ini_path={%s}, log_level={%d}, vmodule={%s}, "
                 "parent_class.fw_name={%s}",
                 dev.ini_path, dev.log_level, dev.vmodule, dev.parent_class.fw_name);
}

namespace goldfish::avd_info {

::android::goldfish::Avd* get_avd();

devices::ConnectorRegistry& deviceRegistry();

void avd_info_register_types(void);

}  // namespace goldfish::avd_info
