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

struct AvdEndDev {
    DeviceClass parent_class;
};

#define TYPE_AVD_FINAL "avdend"
#define AVD_FINAL_INFO_DEV(obj) OBJECT_CHECK(AvdEndDev, (obj), TYPE_AVD_FINAL)
#define AVD_FINAL_INFO_DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(AvdEndDev, obj, TYPE_AVD_FINAL)
