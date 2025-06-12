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

#include "goldfish/avd/avd-finalize.h"

#include "goldfish/avd/avd-info.h"

// clang-format off
// IWYU pragma: begin_keep
// Make sure qemu doesn't override our listen method.

extern "C" {
#include "qemu/osdep.h"
#include "hw/qdev-core.h"

// #ifdef QEMU_OS_WIN32_H
// #undef listen
// #endif
#include "goldfish/devices/connector_registry.h"

}
// IWYU pragma: end_keep
// clang-format on

static void avd_finalize_realize(DeviceState* dev, Error** errp) {
    goldfish::avd_info::deviceRegistry().listen(5000);
}

static void avd_finalize_class_init(ObjectClass* oc, void* data) {
    DeviceClass* dc = DEVICE_CLASS(oc);
    dc->realize = avd_finalize_realize;
}

static const TypeInfo avd_finalize_type_info = {
        .name = TYPE_AVD_FINAL,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(AvdEndDev),
        .class_init = avd_finalize_class_init,
};

static void register_types(void) {
    type_register_static(&avd_finalize_type_info);
}

type_init(register_types);
