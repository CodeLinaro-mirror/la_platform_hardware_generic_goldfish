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
#include "android/goldfish/avd-info.h"

#include <aemu/base/logging/LogSeverity.h>
#include <qapi/error.h>

#include <memory>

#include "absl/log/log.h"
#include "absl/status/statusor.h"

#include "android/goldfish/config/avd.h"

using android::goldfish::Avd;

static std::unique_ptr<Avd> gAvd;

android::goldfish::Avd *get_avd() {
  if (gAvd) {
    return gAvd.get();
  }
  return nullptr;
}

static void avd_info_realize(DeviceState *dev, Error **errp) {
  AvdInfoDev *avd_info = AVD_INFO_DEV(dev);
  auto status = Avd::parse(avd_info->ini_path);
  if (!status.ok()) {
    LOG(FATAL) << "Unable to load: " << avd_info->ini_path
               << " due to: " << status.status().message();
    return;
  }
  LOG(INFO) << "Loaded avd:" << avd_info->ini_path;
  gAvd = std::make_unique<Avd>(std::move(status.value()));
}

static void avd_info_set_ini_path(Object *obj, const char *value,
                                  Error **errp) {
  AvdInfoDev *avd_info = AVD_INFO_DEV(obj);
  LOG(INFO) << "set_ini_path";
  // Construct the std::string member in the pre-allocated memory using
  // placement new
  // new (&avd_info->ini_path)
  avd_info->ini_path = value;
  LOG(INFO) << "set_ini_path: " << avd_info->ini_path;
}

static void avd_info_class_init(ObjectClass *oc, void *data) {
  object_class_property_add_str(oc, "ini_path", NULL, avd_info_set_ini_path);
  object_class_property_set_description(
      oc, "ini_path", "the path to the AVD's configuration (.ini) file.");

  DeviceClass *dc = DEVICE_CLASS(oc);
  dc->realize = avd_info_realize;
}

static const TypeInfo avd_info_type_info = {
    .name = TYPE_AVD,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(AvdInfoDev),
    .class_init = avd_info_class_init,
};

static void register_types(void) { type_register_static(&avd_info_type_info); }

type_init(register_types);
