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

#include "android/goldfish/config/avd.h"

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/atomic.hpp"

extern "C" {
#include "qemu/osdep.h"
#include "hw/qdev-core.h"
#include "qom/object.h"
#include "qapi/error.h"
#include <stdlib.h>
#include <string.h>
}
// IWYU pragma: end_keep
// clang-format on

struct AvdInfoDev {
    DeviceClass parent_class;
    std::string ini_path;
    int log_level{2};     // Log only errors
    std::string vmodule;  // Vlog filter
};
#define TYPE_AVD "avdinfo"
#define AVD_INFO_DEV(obj) OBJECT_CHECK(AvdInfoDev, (obj), TYPE_AVD)
#define AVD_INFO_DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(AvdInfoDev, obj, TYPE_AVD)

android::goldfish::Avd* get_avd();

template <typename Sink>
void AbslStringify(Sink& sink, AvdInfoDev dev) {
    absl::Format(&sink,
                 "AvdInfoDev: ini_path={%s}, log_level={%d}, vmodule={%s}, "
                 "parent_class.fw_name={%s}, parent_class.desc={%s}, ",
                 dev.ini_path, dev.log_level, dev.vmodule, dev.parent_class.fw_name,
                 dev.parent_class.desc);
}