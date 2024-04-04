
// Copyright (C) 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS);
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "android/goldfish/config/emulator.h"

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "aemu/base/logging/Log.h"
#include "aemu/base/process/Command.h"
#include "aemu/base/process/Process.h"
#include "android/base/system/storage_capacity.h"
#include "android/goldfish/config/avd.h"
#include "android/goldfish/devices/audio_device.h"
#include "android/goldfish/devices/cpu_device.h"
#include "android/goldfish/devices/drives/cache_drive.h"
#include "android/goldfish/devices/drives/disk_drive.h"
#include "android/goldfish/devices/drives/encryption_drive.h"
#include "android/goldfish/devices/drives/sdcard_drive.h"
#include "android/goldfish/devices/drives/user_data_drive.h"
#include "android/goldfish/devices/gpu_device.h"
#include "android/goldfish/devices/initrd_device.h"
#include "android/goldfish/devices/kernel_device.h"
#include "android/goldfish/devices/machine.h"
#include "android/goldfish/devices/memory_device.h"
#include "android/goldfish/devices/parameter_list.h"

#include <android/base/system/System.h>
#include <istream>
#include <memory>
#include <stdio.h>
#include <string_view>

namespace android::goldfish {

using android::base::operator""_KiB;

Emulator::Emulator(Avd avd) : mAvd(std::move(avd)) {

  mDevices.emplace_back(std::make_unique<Machine>());
  mDevices.emplace_back(std::make_unique<CpuDevice>());
  mDevices.emplace_back(std::make_unique<MemoryDevice>());
  mDevices.emplace_back(std::make_unique<KernelDevice>());
  mDevices.emplace_back(std::make_unique<Initrd>());
  mDevices.emplace_back(std::make_unique<GpuDevice>());
  mDevices.emplace_back(
      std::make_unique<RawDrive>("system", "03.0", Avd::ImageType::INITSYSTEM));
  mDevices.emplace_back(
      std::make_unique<RawDrive>("vendor", "07.0", Avd::ImageType::INITVENDOR));
  mDevices.emplace_back(std::make_unique<UserDataDrive>(avd.hw()));
  mDevices.emplace_back(std::make_unique<EncryptionDrive>(avd.hw()));
  mDevices.emplace_back(std::make_unique<CacheDrive>(avd.hw()));
  mDevices.emplace_back(std::make_unique<SDCardDrive>(avd.hw()));
  mDevices.emplace_back(std::make_unique<AudioDevice>("09.0"));
  mDevices.emplace_back(
      std::make_unique<ParameterList>(std::vector<std::string>{
          "-serial", "stdio", "-nodefaults", "-no-reboot",
          //     // Debug monitor
          "-monitor", "telnet::45454,server,nowait", "-device",
          "virtio-keyboard-pci",
          //     // Series of simple devices that don't need configuring
          "-device", "virtio-serial,ioeventfd=off", "-device",
          "virtio-rng-pci"}));

  for (auto &device : mDevices) {
    mDeviceMap[device->id()] = device.get();
  }
}

void Emulator::clear() {
  for (auto &device : mDevices) {
    dinfo("Reset: %s", device->id());
    device->clear();
  }
}

absl::Status Emulator::initialize() {
  for (auto &device : mDevices) {
    dinfo("Preparing: %s", device->id());
    auto status = device->initialize(*this);
    if (!status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

std::vector<std::string> Emulator::getCmdline() {
  std::vector<std::string> params{get<Machine>("machine")->qemu_binary()};
  for (auto &device : mDevices) {
    auto component = device->getQemuParameters(*this);
    params.insert(params.end(), component.begin(), component.end());
  }

  return params;
}

absl::Status Emulator::launch() {
  dinfo("Preparing %s", mAvd.details());
  auto status = initialize();
  if (!status.ok()) {
    dinfo("Failed to prepare emulator: %s", status.message());
    return status;
  }

  auto args = getCmdline();

  // TODO(jansene): Setup dll load paths.

  dinfo("Launch: %s", absl::StrJoin(args, " "));
  auto proc = android::base::Command::create(getCmdline())
                  .withStdoutBuffer((size_t)128_KiB)
                  .withStderrBuffer((size_t)128_KiB)
                  .execute();
  dinfo("Launched qemu as pid: %d", proc->pid());
  std::istream &stream = proc->out()->asStream();
  while (stream.good()) {
    printf("%c", stream.get());
  }

  // Errors usually end up here
  dinfo("%s", proc->err()->asString());
  dinfo("Finished: %d", proc->exitCode());
  return absl::OkStatus();
}
} // namespace android::goldfish