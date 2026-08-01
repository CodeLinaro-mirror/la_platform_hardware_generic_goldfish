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

#include "goldfish/avd_finalize/avd_finalize.h"

#include "goldfish/avd_info/avd_private.h"

// clang-format off
// IWYU pragma: begin_keep
#include "qemu/osdep.h"
extern "C" {
#include "hw/core/qdev.h"
}
#undef listen
// IWYU pragma: end_keep
// clang-format on

namespace goldfish::avd_finalize {
namespace {

struct AvdEndDev {
    DeviceClass parent_class;
};

#define TYPE_AVD_FINAL "avdend"
#define AVD_FINAL_INFO_DEV(obj) OBJECT_CHECK(AvdEndDev, (obj), TYPE_AVD_FINAL)
#define AVD_FINAL_INFO_DEVICE_GET_CLASS(obj) OBJECT_GET_CLASS(AvdEndDev, obj, TYPE_AVD_FINAL)

void avd_finalize_realize(DeviceState* dev, Error** errp) {
    goldfish::avd_info::UniverseBuildComplete();
}

void avd_finalize_class_init(ObjectClass* oc, const void* data) {
    DeviceClass* dc = DEVICE_CLASS(oc);
    dc->realize = avd_finalize_realize;
}

const TypeInfo avd_finalize_type_info = {
    .name = TYPE_AVD_FINAL,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(AvdEndDev),
    .class_init = avd_finalize_class_init,
};

}  // namespace

void avd_finalize_register_types(void) {
    type_register_static(&avd_finalize_type_info);
}

}  // namespace goldfish::avd_finalize
