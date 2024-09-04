
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

#include <android/base/system/System.h>
#include <stdio.h>

#include <initializer_list>
#include <istream>
#include <memory>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include "aemu/base/logging/Log.h"
#include "aemu/base/process/Command.h"
#include "aemu/base/process/Process.h"
#include "android/base/bazel/bazel_info.h"
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
#include "android/goldfish/devices/grpc_device.h"
#include "android/goldfish/devices/initrd_device.h"
#include "android/goldfish/devices/kernel_device.h"
#include "android/goldfish/devices/machine.h"
#include "android/goldfish/devices/memory_device.h"
#include "android/goldfish/devices/parameter_list.h"

namespace android::goldfish {

using android::base::operator""_KiB;
using android::base::Bazel;
using android::base::System;

Emulator::Emulator(Avd avd, std::vector<std::string> additionalParams) : mAvd(std::move(avd)) {
    // Device are initialized in order of appearance
    // So if device B depends on device A, you should register them as:
    // -device A -device B ...

    mDevices.emplace_back(std::make_unique<Machine>());
    mDevices.emplace_back(std::make_unique<CpuDevice>());

    auto ini_path = System::pathAsString(mAvd.getIniFile());
    mDevices.emplace_back(std::make_unique<ParameterList>(std::initializer_list<std::string>{
            "-device", absl::StrCat("avdinfo,ini_path=", ini_path)}));
    mDevices.emplace_back(std::make_unique<MemoryDevice>());
    mDevices.emplace_back(std::make_unique<KernelDevice>());
    mDevices.emplace_back(std::make_unique<Initrd>());
    mDevices.emplace_back(std::make_unique<GpuDevice>());
    mDevices.emplace_back(std::make_unique<RawDrive>("system", "03.0", Avd::ImageType::INITSYSTEM));
    mDevices.emplace_back(std::make_unique<RawDrive>("vendor", "07.0", Avd::ImageType::INITVENDOR));
    mDevices.emplace_back(std::make_unique<UserDataDrive>(avd.hw()));
    mDevices.emplace_back(std::make_unique<EncryptionDrive>(avd.hw()));
    mDevices.emplace_back(std::make_unique<CacheDrive>(avd.hw()));
    mDevices.emplace_back(std::make_unique<SDCardDrive>(avd.hw()));
    mDevices.emplace_back(std::make_unique<AudioDevice>("09.0"));
    mDevices.emplace_back(std::make_unique<GrpcDevice>());

    auto simple_parameters =
            std::vector<std::string>{"-serial", "stdio", "-nodefaults", "-no-reboot",
                                     // Debug monitor
                                     "-monitor", "telnet::45454,server,nowait",
                                     // our virtio-vsock
                                     "-device", "virtio-goldfish-vsock-pci,guest-cid=3",
                                     // Keyboard
                                     "-device", "virtio-keyboard-pci",
                                     // Series of simple devices that don't need configuring
                                     "-device", "virtio-serial-pci,ioeventfd=off",
                                     // Hardware RNG device
                                     "-device", "virtio-rng-pci",
                                     // virtio logcat consoles, note that order matters here!
                                     "-device", "virtconsole,chardev=forhvc0",
                                     //
                                     "-device", "virtconsole,chardev=forhvc1",
                                     //
                                     "-chardev", "null,id=forhvc0"};

    if (Bazel::inBazel()) {
        // We are running in the bazel environment, add the bios to the search path.
        fs::path bios_path = fs::path(Bazel::runfilesPath("_main/external/qemu/pc-bios"));
        assert(fs::exists(bios_path));

        simple_parameters.push_back("-L");
        simple_parameters.push_back(System::pathAsString(bios_path));
    }
    simple_parameters.insert(simple_parameters.end(),
                             std::make_move_iterator(additionalParams.begin()),
                             std::make_move_iterator(additionalParams.end()));

    mDevices.emplace_back(std::make_unique<ParameterList>(std::move(simple_parameters)));
    for (auto& device : mDevices) {
        mDeviceMap[device->id()] = device.get();
    }
}

void Emulator::clear() {
    for (auto& device : mDevices) {
        LOG(INFO) << "Reset: " << device->id();
        device->clear();
    }
}

absl::Status Emulator::initialize() {
    for (auto& device : mDevices) {
        LOG(INFO) << "Preparing: " << device->id();
        auto status = device->initialize(*this);
        if (!status.ok()) {
            return status;
        }
    }
    return absl::OkStatus();
}

std::vector<std::string> Emulator::getCmdline() {
    std::vector<std::string> params{get<Machine>("machine")->qemu_binary().string()};
    for (auto& device : mDevices) {
        auto component = device->getQemuParameters(*this);
        params.insert(params.end(), component.begin(), component.end());
    }

    return params;
}

absl::Status Emulator::launch() {
    LOG(INFO) << "Preparing " << mAvd.details();
    auto status = initialize();
    if (!status.ok()) {
        LOG(INFO) << "Failed to prepare emulator: " << status.message();
        return status;
    }

    auto args = getCmdline();

    // Setup the library search dirs.
    fs::path qemu_module_dir;
    if (Bazel::inBazel()) {
        // We are running in the bazel environment, make sure the plugins can be
        // found.
        qemu_module_dir = fs::path(
                Bazel::runfilesPath("_main/hardware/generic/goldfish/emulator/launcher/plugins"));
        assert(fs::exists(qemu_module_dir));
    } else {
        qemu_module_dir = System::get()->getProgramDirectory() / "lib" / "qemu";
    }

    System::get()->setEnvironmentVariable("QEMU_MODULE_DIR", System::pathAsString(qemu_module_dir));
    System::get()->addLibrarySearchDir(qemu_module_dir);
    LOG(INFO) << "Using module dir: " << qemu_module_dir;
    LOG(INFO) << "Launch: " << absl::StrJoin(args, " ");
    auto proc = android::base::Command::create(getCmdline()).replace().execute();
    // We only get here if we failed to launch the application
    return absl::InternalError(absl::StrFormat("Failed to launch emulator, error code: %d", errno));
}
}  // namespace android::goldfish
