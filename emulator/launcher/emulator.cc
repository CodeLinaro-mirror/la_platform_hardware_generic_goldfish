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

#include "emulator.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <sstream>
#include <string_view>
#include <vector>

// Use ABSL_LOG to avoid conflict with crashpadh logging
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include "android/process/command.h"
#include "android/status/status_macros.h"
#include "android/base/system.h"
#include "android/goldfish/avd.h"
#include "configure_drives.h"
#include "devices/adb_device.h"
#include "devices/audio_device.h"
#include "devices/avd_info_device.h"
#include "devices/cpu_device.h"
#include "devices/display_device.h"
#include "devices/gpu_device.h"
#include "devices/grpc_device.h"
#include "devices/initrd_device.h"
#include "devices/kernel_device.h"
#include "devices/machine.h"
#include "devices/memory_device.h"
#include "devices/network_device.h"
#include "devices/parameter_list.h"
#include "devices/wifi_device.h"

namespace android::goldfish {

using android::base::System;

absl::Status Emulator::addDevices() {
    // Device are initialized in order of appearance
    // So if device B depends on device A, you should register them as:
    // -device A -device B ...
    const auto& o = opts();
    const auto& a = avd();
    int pluginLogLevel = static_cast<int>(o.verbose ? absl::LogSeverityAtLeast::kInfo
                                                    : absl::LogSeverityAtLeast::kWarning);

    std::string vmodules = o.vmodule ? o.vmodule : "";
    if (System::Get()->GetEnvironmentVariable("AEMU_LOG_LEVEL").empty()) {
        System::Get()->SetEnvironmentVariable("AEMU_LOG_LEVEL", absl::StrCat(pluginLogLevel));
    }
    if (System::Get()->GetEnvironmentVariable("AEMU_VLOG_LEVEL").empty()) {
        System::Get()->SetEnvironmentVariable("AEMU_VLOG_LEVEL", absl::StrCat(o.V ? o.V : ""));
    }
    if (System::Get()->GetEnvironmentVariable("AEMU_VMODULE").empty()) {
        System::Get()->SetEnvironmentVariable("AEMU_VMODULE", vmodules);
    }
    if (System::Get()->GetEnvironmentVariable("AEMU_LOG_DETAILED").empty()) {
        System::Get()->SetEnvironmentVariable("AEMU_LOG_DETAILED", o.verbose ? "true" : "false");
    }

    addDevice<ParameterList>(std::initializer_list<std::string>{
        "-nodefaults",
        // our iothread
        "-object",
        "iothread,id=disk-iothread",
    });

    addDevice<ParameterList>(std::initializer_list<std::string>{
        "-name", absl::StrFormat("%s,debug-threads=on", a.Name())});
    addDevice<Machine>();
    addDevice<CpuDevice>();
    addDevice<MemoryDevice>();
    addDevice<KernelDevice>();
    addDevice<InitrdDevice>();

    RETURN_IF_ERROR(addDrives(*this));

    addDevice<AudioDevice>("09.0");

    // Hardware RNG device
    addDevice<ParameterList>(std::initializer_list<std::string>{
        "-device",
        "virtio-rng-pci",
    });

    // This is needed for virtconsole (logcat, bt, uwb).
    // TODO old emulator also created a virtio-serial device, do we need to?
    addDevice<ParameterList>(std::initializer_list<std::string>{
        "-device",
        "virtio-serial-pci,ioeventfd=off",
    });

    // virtio logcat consoles, note that order matters here!
    // This device is probably just to make sure that logcat is on device 1 and not 0.
    addDevice<ParameterList>(std::initializer_list<std::string>{
        "-device",
        "virtconsole,chardev=forhvc0,name=logcat_null",
        "-chardev",
        "null,id=forhvc0",
    });
    if (o.logcat_output) {
        // virtio logcat consoles, note that order matters here!
        addDevice<ParameterList>(std::initializer_list<std::string>{
            // Actual logcat location.
            "-device", "virtconsole,chardev=forhvc1,name=logcat", "-chardev",
            absl::StrCat("file,id=forhvc1,path=", o.logcat_output)});
    } else {
        addDevice<ParameterList>(std::initializer_list<std::string>{
            // Actual logcat location.
            "-device", "virtconsole,chardev=forhvc1,name=logcat", "-chardev", "null,id=forhvc1"});
    }

    if (o.show_kernel) {
        addDevice<ParameterList>(std::initializer_list<std::string>{"-serial", "stdio"});
    }

    addDevice<AvdInfoDevice>();

    // No ethernet device for now:
    // addDevice<NetworkDevice>("0a.0");

    if (!o.no_netsim) {
        addDevice<ParameterList>(std::initializer_list<std::string>{
            "-device",
            absl::StrCat("netsim-connection,id=netsim,grpc_endpoint=", chardev_endpoints().netsim),
        });

        if (!o.no_wifi) {
            addDevice<WifiDevice>("0b.0");
        }
        // The name of these vport devices should be used by
        // http://ac/device/generic/goldfish/qemu-props/vport_parser.cpp
        // It should lookup the actual port number and set the property
        // "vendor.qemu.vport.<name>" to "/dev/vport8p<N>"
        // e.g. /dev/vport8p3 for bt (4th port)
        addDevice<ParameterList>(std::initializer_list<std::string>{
            "-chardev", "netsim-uwb,id=uwb",
            "-device", "virtconsole,chardev=uwb,name=uwb",

            "-chardev", "netsim-bt,id=bluetooth",
            "-device", "virtserialport,chardev=bluetooth,name=bluetooth",
        });
    }

    if (!chardev_endpoints().modem_simulator.empty()) {
        addDevice<ParameterList>(std::initializer_list<std::string>{
            "-chardev",
            absl::StrCat("socket,id=modem,nodelay=on,reconnect-ms=100,",
                         chardev_endpoints().modem_simulator),
            "-device",
            "virtserialport,chardev=modem,name=modem",
        });
    }

    std::string gpu_name = "gpu0";
    addDevice<GpuDevice>(gpu_name);

    // Also includes input devices for the display.
    addDevice<DisplayDevice>(gpu_name);

    // Our virtio-vsock.
    addDevice<ParameterList>(std::initializer_list<std::string>{
        "-device",
        "virtio-goldfish-vsock-pci,guest-cid=3",
    });

    // Must come after vsock.
    addDevice<AdbDevice>();

    // Make sure we have our other devices available before we setup the gRPC device, the gRPC
    // device depends on the virtio devices for input event delivery.
    if (!o.no_grpc) {
        addDevice<GrpcDevice>();
    }

    // This should always be the last device, as it will finalize android emulator initialization.
    addDevice<ParameterList>(std::initializer_list<std::string>{"-device", "avdend"});

    addDevice<ParameterList>(
            std::initializer_list<std::string>{"-L", paths().bios_directory.string()});

    if (o.qemu_telnet) {
        // Debug monitor
        addDevice<ParameterList>(std::initializer_list<std::string>{
            "-monitor",
            "telnet::15454,server,nowait",
        });
    }

    const bool snapshot_save_needed = o.snapshot && o.snapshot[0] != '\0' && !o.no_snapshot_save;
    const bool qmp_needed = snapshot_save_needed && qmp_port() != 0;
    if (qmp_needed) {
        addDevice<ParameterList>(std::initializer_list<std::string>{
            "-qmp",
            absl::StrFormat("tcp:127.0.0.1:%d,server,nowait", qmp_port()),
        });
    }

    if (o.snapshot && o.snapshot[0] != '\0') {
        if (o.no_snapshot_load) {
            LOG(INFO) << "Snapshot loading disabled by -no-snapshot-load, performing cold boot.";
        } else if (snapshotExists(o.snapshot)) {
            LOG(INFO) << "Snapshot '" << o.snapshot << "' found, loading...";
            addDevice<ParameterList>(std::initializer_list<std::string>{"-loadvm", o.snapshot});
        } else {
            LOG(INFO) << "Snapshot '" << o.snapshot << "' not found, performing cold boot.";
        }
    }

    if (o.qemu) {
        addDevice<ParameterList>(absl::StrSplit(o.qemu, ' '));
    }

    return absl::OkStatus();
}

bool Emulator::snapshotExists(const std::string& name) const {
    const auto& a = avd();
    fs::path bootstatus_ini = a.GetContentPath() / "snapshots" / name / "bootstatus.ini";
    return fs::exists(bootstatus_ini);
}

void Emulator::clear() {
    for (auto& device : mDevices) {
        ABSL_LOG(INFO) << "Reset: " << device->id();
        device->clear();
    }
}

absl::Status Emulator::initialize() {
    RETURN_IF_ERROR(addDevices());

    for (auto& device : mDevices) {
        ABSL_LOG(INFO) << "Preparing: " << device->id();
        auto status = device->initialize(*this);
        if (!status.ok()) {
            return status;
        }
    }
    return absl::OkStatus();
}

std::string Emulator::qemu_exe_path() const {
    const auto& p = paths();
    std::string base;
    switch (avd().DetectArchitecture()) {
    case Avd::CpuArchitecture::kX86:
        return p.qemu_system_x86_binary.string();
    case Avd::CpuArchitecture::kArm:
        return p.qemu_system_arm_binary.string();
    case Avd::CpuArchitecture::kRiscV:
        return p.qemu_system_riscv_binary.string();
    default:
        return "unknown";
    }

    return base;
}

std::vector<std::string> Emulator::getCmdline() const {
    std::vector<std::string> params;

    for (const auto& device : mDevices) {
        auto component = device->getQemuParameters(*this);
        params.insert(params.end(), component.begin(), component.end());
    }

    return params;
}

absl::StatusOr<::goldfish::async::LaunchConfig> Emulator::launch_config() {
    const auto& o = opts();
    const auto& a = avd();
    ABSL_LOG(INFO) << "Preparing " << a.Details(true);
    auto status = initialize();
    if (!status.ok()) {
        ABSL_LOG(INFO) << "Failed to prepare emulator: " << status.message();
        return status;
    }

    // TODO(b/418838762): Move these to the gpu device once devices can supply env vars to set.
    // Graphics default to software rendering (with swangle) for now.
    // Always indirect EGL.
    System::Get()->SetEnvironmentVariable("ANDROID_EGL_ON_EGL", "1");

#if defined(__linux__)
    // on linux, default to use swiftshader_indirect for gl,
    // later gl will be removed once vulkan composition is on
    System::Get()->SetEnvironmentVariable("ANDROID_EMU_RENDERER", "swiftshader");
#else
    // ANGLE works fine on mac/windows on top of lavapipe, no need to change it
    // in addition, swiftshader does not work on mac anyway
    System::Get()->SetEnvironmentVariable("ANDROID_EMU_RENDERER", "swangle");
    System::Get()->SetEnvironmentVariable("ANGLE_DEFAULT_PLATFORM", "vulkan");
#endif

    // now all default to lavapipe
    System::Get()->SetEnvironmentVariable("ANDROID_EMU_VK_ICD", "lavapipe");

    if (bool gpu_host = o.gpu && std::string(o.gpu) == "host"; gpu_host) {
        System::Get()->SetEnvironmentVariable("ANGLE_DEFAULT_PLATFORM", "vulkan");
#if defined(__APPLE__)
        System::Get()->SetEnvironmentVariable("ANDROID_EMU_VK_ICD", "moltenvk");
#else
        System::Get()->SetEnvironmentVariable("ANDROID_EMU_VK_ICD", "");
#endif
    }

    fs::path exe_path = qemu_exe_path();
    std::vector<std::string> args = getCmdline();
    {
        std::vector<std::string> printableArgs;
        printableArgs.reserve(args.size() + 1);
        printableArgs.push_back(exe_path.string());
        std::transform(args.begin(), args.end(), std::back_inserter(printableArgs),
                       [](const std::string& a) -> std::string {
                           if (std::any_of(a.begin(), a.end(),
                                           [](const char c) { return std::isspace(c); })) {
                               using namespace std::literals::string_literals;
                               return "\""s + a + "\""s;
                           } else {
                               return a;
                           }
                       });

        ABSL_LOG(INFO) << "Emulator launch command: " << absl::StrJoin(printableArgs, " ");
    }

    if (o.wait_for_debugger) {
        // This is used to wait for the debugger at qemu process start.
        System::Get()->SetEnvironmentVariable("ANDROID_EMU_WAIT_FOR_DEBUGGER", "1");
    }

    return ::goldfish::async::LaunchConfig{
        .exe_path = std::move(exe_path),
        .args = std::move(args),
        .new_process_group = true,
        .stdio_mode = ::goldfish::async::LaunchConfig::StdioMode::kInherit,
    };
}

}  // namespace android::goldfish
