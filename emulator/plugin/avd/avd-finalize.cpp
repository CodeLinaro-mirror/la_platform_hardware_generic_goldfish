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
#include "qemu/osdep.h"
extern "C" {
#include "hw/qdev-core.h"
}
#undef listen
// IWYU pragma: end_keep
// clang-format on

namespace goldfish::avd_finalize {
namespace {
void avd_finalize_realize(DeviceState* dev, Error** errp) {
    goldfish::avd_info::connector_registry().listen(5000);
}

void avd_finalize_class_init(ObjectClass* oc, void* data) {
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
