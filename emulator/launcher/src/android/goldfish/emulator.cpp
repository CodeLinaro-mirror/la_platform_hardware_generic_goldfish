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

#include <stdio.h>

#include <algorithm>
#include <initializer_list>
#include <istream>
#include <memory>
#include <sstream>
#include <string_view>
#include <vector>

// Use ABSL_LOG to avoid conflict with crashpadh logging
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include "aemu/base/process/Command.h"
#include "aemu/base/process/Process.h"
#include "android/base/bazel/bazel_info.h"
#include "android/base/system/System.h"
#include "android/base/system/storage_capacity.h"
#include "android/crashreport/CrashReporter.h"
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

Emulator::Emulator(Avd avd, AndroidOptions opts) : mAvd(std::move(avd)), mOpts(std::move(opts)) {
    // Device are initialized in order of appearance
    // So if device B depends on device A, you should register them as:
    // -device A -device B ...

    absl::LogSeverityAtLeast logLevel =
            opts.verbose ? absl::LogSeverityAtLeast::kInfo : absl::LogSeverityAtLeast::kWarning;

    std::string vmodules = opts.vmodule ? opts.vmodule : "";
    std::replace(vmodules.begin(), vmodules.end(), ',', '|');
    auto ini_path = System::pathAsString(mAvd.getIniFile());

    addDevice<ParameterList>(std::initializer_list<std::string>{
            "-name", absl::StrFormat("%s,debug-threads=on", mAvd.name())});
    addDevice<Machine>();
    addDevice<CpuDevice>();
    addDevice<MemoryDevice>();
    addDevice<KernelDevice>();
    addDevice<Initrd>();
    addDevice<GpuDevice>();
    addDevice<RawDrive>("system", "03.0", Avd::ImageType::INITSYSTEM);
    addDevice<RawDrive>("vendor", "07.0", Avd::ImageType::INITVENDOR);
    addDevice<UserDataDrive>(mAvd.hw());
    addDevice<EncryptionDrive>(mAvd.hw());
    addDevice<CacheDrive>(mAvd.hw());
    addDevice<SDCardDrive>(mAvd.hw());
    addDevice<AudioDevice>("09.0");

    addDevice<ParameterList>(std::initializer_list<std::string>{
            "-device", absl::StrFormat("avdstart,ini_path=%s,vmodule=%s,log_level=%d", ini_path,
                                       vmodules, logLevel)});
    addDevice<GrpcDevice>();

    addDevice<ParameterList>(std::initializer_list<std::string>{
            "-nodefaults", "-no-reboot",
            // Debug monitor
            "-monitor", "telnet::15454,server,nowait",
            // our virtio-vsock
            "-device", "virtio-goldfish-vsock-pci,guest-cid=3",
            // // TODO(jansene): host_port should be dynamic..
            "-device", "virtio-goldfish-adb,host_port=5555",
            // Keyboard
            "-device", "virtio-keyboard-pci,head=0,display=gpu0",
            // Series of simple devices that don't need configuring
            "-device", "virtio-serial-pci,ioeventfd=off",
            // Hardware RNG device
            "-device", "virtio-rng-pci",
            // ...
    });

    // Add our virtio devices, we connect them in QEMU to gpu0 and head=%d so qemu knows how to
    // route input events for a given display to the proper device.
    constexpr int VIRTIO_INPUT_MAX_NUM = 11;
    for (int id = 0; id < VIRTIO_INPUT_MAX_NUM; id++) {
        addDevice<ParameterList>(std::initializer_list<std::string>{
                "-device", absl::StrFormat("virtio-input-android-pci,display=gpu0,head=%d", id)});
    }

#if defined(__linux__) || defined(__APPLE__)
    // This ensures that only users on local box with read/write access to that path can access the
    // VNC server. Ports can be forwarded with ssh.
    // TODO(jansene):  we technically should force display=gpu0,head=0, to use proper qemu console
    // routing. However it seems that the gpu0 is not yet ready at time of vnc registration.
    addDevice<ParameterList>(std::initializer_list<std::string>{
            "-display",
            "vnc=unix:/tmp/.qemu-emu-vnc,display=gpu0,head=0",
    });
    ABSL_LOG(INFO) << "VNC will be available on /tmp/.qemu-emu-vnc";
    ABSL_LOG(INFO)
            << "Tunnel over ssh with: `ssh -L localhost:5901:/tmp/.qemu-emu-vnc <remote-host>``";
    ABSL_LOG(INFO) << "Or run `socat TCP-LISTEN:5901,fork,reuseaddr "
                      "UNIX-CONNECT:/tmp/.qemu-emu-vnc` for buggy vnc viewers.";
#endif

    if (opts.logcat_output) {
        // virtio logcat consoles, note that order matters here!
        addDevice<ParameterList>(std::initializer_list<std::string>{
                "-device", "virtconsole,chardev=forhvc0", "-chardev", "null,id=forhvc0",
                // Actual logcat location.
                "-device", "virtconsole,chardev=forhvc1", "-chardev",
                absl::StrCat("file,id=forhvc1,path=", opts.logcat_output)});
    }

    if (opts.show_kernel) {
        addDevice<ParameterList>(std::initializer_list<std::string>{"-serial", "stdio"});
    }
    if (Bazel::inBazel()) {
        // We are running in the bazel environment, add the bios to the search path.
        fs::path bios_path = fs::path(Bazel::runfilesPath("_main/external/qemu/pc-bios"));
        assert(fs::exists(bios_path));
        addDevice<ParameterList>(
                std::initializer_list<std::string>{"-L", System::pathAsString(bios_path)});
    }

    // This should always be the last device, as it will finalize android emulator initialization
    addDevice<ParameterList>(std::initializer_list<std::string>{"-device", "avdend"});
}

void Emulator::clear() {
    for (auto& device : mDevices) {
        ABSL_LOG(INFO) << "Reset: " << device->id();
        device->clear();
    }
}

absl::Status Emulator::initialize() {
    for (auto& device : mDevices) {
        ABSL_LOG(INFO) << "Preparing: " << device->id();
        auto status = device->initialize(*this);
        if (!status.ok()) {
            return status;
        }
    }
    return absl::OkStatus();
}

std::vector<std::string> Emulator::getCmdline() const {
    std::vector<std::string> params{get<Machine>("machine")->qemu_binary().string()};
    for (const auto& device : mDevices) {
        auto component = device->getQemuParameters(*this);
        params.insert(params.end(), component.begin(), component.end());
    }

    return params;
}

absl::Status Emulator::launch() {
    ABSL_LOG(INFO) << "Preparing " << mAvd.details(true);
    auto status = initialize();
    if (!status.ok()) {
        ABSL_LOG(INFO) << "Failed to prepare emulator: " << status.message();
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

    // Make sure the child process is using the same crashpad handler as we are using.
    std::stringstream handler;
    handler << android::crashreport::CrashReporter::handlerExe();
    System::get()->setEnvironmentVariable("AEMU_CRASHPAD_HANDLER", handler.str());
    System::get()->setEnvironmentVariable("QEMU_MODULE_DIR", System::pathAsString(qemu_module_dir));
    System::get()->addLibrarySearchDir(qemu_module_dir);

    ABSL_LOG(INFO) << "Using crashpad handler: " << handler.str();
    ABSL_LOG(INFO) << "Using module dir: " << qemu_module_dir;
    ABSL_LOG(INFO) << "Launch: " << absl::StrJoin(args, " ");

    auto proc = android::base::Command::create(getCmdline()).replace().execute();
    // We only get here if we failed to launch the application
    return absl::InternalError(absl::StrFormat("Failed to launch emulator, error code: %d", errno));
}
}  // namespace android::goldfish
