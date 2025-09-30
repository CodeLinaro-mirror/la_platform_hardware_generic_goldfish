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

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

// Use ABSL_LOG to avoid conflict with crashpadh logging
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include "aemu/base/process/Command.h"
#include "aemu/base/process/Process.h"
#include "aemu/base/utils/status_macros.h"
#include "android/base/bazel/bazel_info.h"
#include "android/base/system/System.h"
#include "android/goldfish/config/avd.h"
#include "devices/adb_device.h"
#include "devices/audio_device.h"
#include "devices/cpu_device.h"
#include "devices/display_device.h"
#include "devices/drives/configure_drives.h"
#include "devices/gpu_device.h"
#include "devices/grpc_device.h"
#include "devices/initrd_device.h"
#include "devices/kernel_device.h"
#include "devices/machine.h"
#include "devices/memory_device.h"
#include "devices/network_device.h"
#include "devices/parameter_list.h"

namespace android::goldfish {

using android::base::Bazel;
using android::base::System;

absl::Status Emulator::addDevices() {
    // Device are initialized in order of appearance
    // So if device B depends on device A, you should register them as:
    // -device A -device B ...
    absl::LogSeverityAtLeast pluginLogLevel =
            mOpts.verbose ? absl::LogSeverityAtLeast::kInfo : absl::LogSeverityAtLeast::kWarning;

    std::string vmodules = mOpts.vmodule ? mOpts.vmodule : "";
    std::replace(vmodules.begin(), vmodules.end(), ',', '|');

    addDevice<ParameterList>(std::initializer_list<std::string>{
        "-nodefaults",
        "-no-reboot",
        // our iothread
        "-object", "iothread,id=disk-iothread",
    });

    addDevice<ParameterList>(std::initializer_list<std::string>{
            "-name", absl::StrFormat("%s,debug-threads=on", mAvd->name())});
    addDevice<Machine>();
    addDevice<CpuDevice>();
    addDevice<MemoryDevice>();
    addDevice<KernelDevice>();
    addDevice<InitrdDevice>();

    RETURN_IF_ERROR(addDrives(*this));

    addDevice<AudioDevice>("09.0");

    addDevice<NetworkDevice>("0a.0");

    // Hardware RNG device
    addDevice<ParameterList>(std::initializer_list<std::string>{
        "-device", "virtio-rng-pci",
    });

    // This is needed for virtconsole (logcat).
    addDevice<ParameterList>(std::initializer_list<std::string>{
        "-device", "virtio-serial-pci,ioeventfd=off",
    });

    if (mOpts.logcat_output) {
        // virtio logcat consoles, note that order matters here!
        addDevice<ParameterList>(std::initializer_list<std::string>{
            "-device", "virtconsole,chardev=forhvc0", "-chardev", "null,id=forhvc0",
            // Actual logcat location.
            "-device", "virtconsole,chardev=forhvc1", "-chardev",
            absl::StrCat("file,id=forhvc1,path=", mOpts.logcat_output)});
    }

    if (mOpts.show_kernel) {
        addDevice<ParameterList>(std::initializer_list<std::string>{"-serial", "stdio"});
    }

    auto ini_path = System::pathAsString(mAvd->getIniFile());
    addDevice<ParameterList>(std::initializer_list<std::string>{
            "-device", absl::StrFormat("avdstart,ini_path=%s,vmodule=%s,log_level=%d", ini_path,
                                       vmodules, pluginLogLevel)});

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
    addDevice<GrpcDevice>();

    // This should always be the last device, as it will finalize android emulator initialization.
    addDevice<ParameterList>(std::initializer_list<std::string>{"-device", "avdend"});

    if (Bazel::inBazel()) {
        // We are running in the bazel environment, add the bios to the search path.
        // This is necessary because Qemu searches relative to the current executable path which is
        // canonicalized to resolve all symlinks but in Bazel the launcher directory tree is composed
        // of symlinks so the link to the launcher directory is lost.
        addDevice<ParameterList>(std::initializer_list<std::string>{"-L", System::pathAsString(mResolvedPaths.bios_directory)});
    }

    if (mOpts.qemu_telnet) {
        // Debug monitor
        addDevice<ParameterList>(std::initializer_list<std::string>{
            "-monitor", "telnet::15454,server,nowait",
        });
    }

    if (mOpts.qemu) {
        addDevice<ParameterList>(absl::StrSplit(mOpts.qemu, ' '));
    }

    return absl::OkStatus();
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
    std::string base;
    switch (mAvd->detectArchitecture()) {
        case Avd::CpuArchitecture::kX86:
            return mResolvedPaths.qemu_system_x86_binary.string();
        case Avd::CpuArchitecture::kArm:
            return mResolvedPaths.qemu_system_arm_binary.string();
        case Avd::CpuArchitecture::kRiscV:
            return mResolvedPaths.qemu_system_riscv_binary.string();
        default:
            return "unknown";
    }

  return base;
}

std::vector<std::string> Emulator::getCmdline() const {
    std::vector<std::string> params{qemu_exe_path()};

    for (const auto& device : mDevices) {
        auto component = device->getQemuParameters(*this);
        params.insert(params.end(), component.begin(), component.end());
    }

    return params;
}

absl::Status Emulator::launch() {
    ABSL_LOG(INFO) << "Preparing " << mAvd->details(true);
    auto status = initialize();
    if (!status.ok()) {
        ABSL_LOG(INFO) << "Failed to prepare emulator: " << status.message();
        return status;
    }

    // TODO(b/418838762): Move these to the gpu device once devices can supply env vars to set.
    // Graphics default to software rendering (with swangle) for now.
    // Always indirect EGL.
    System::get()->setEnvironmentVariable("ANDROID_EGL_ON_EGL", "1");

#if defined(__linux__) || defined(__APPLE__)
    const char* kXDG_RUNTIME_DIR_NAME = "XDG_RUNTIME_DIR";
    const char* xdg_runtime_dir_val = getenv(kXDG_RUNTIME_DIR_NAME);
    if (!xdg_runtime_dir_val) {
        const char* default_runtime_dir = "/tmp";
#if defined(__APPLE__)
        const char* darwin_runtime_dir = getenv("DARWIN_USER_TEMP_DIR");
        if (darwin_runtime_dir) {
            default_runtime_dir = darwin_runtime_dir;
        } else {
            const char* darwin_temp_dir = getenv("TMPDIR");
            if (darwin_temp_dir) {
                default_runtime_dir = darwin_temp_dir;
            }
        }
#endif
        System::get()->setEnvironmentVariable(kXDG_RUNTIME_DIR_NAME, default_runtime_dir);
    }
#endif

#if defined(__linux__)
    // on linux, default to use swiftshader_indirect for gl,
    // later gl will be removed once vulkan composition is on
    System::get()->setEnvironmentVariable("ANDROID_EMU_RENDERER", "swiftshader_indirect");
    System::get()->setEnvironmentVariable("ANDROID_EMU_VK_ICD", "lavapipe");
#elif defined(__APPLE__)
    System::get()->setEnvironmentVariable("ANDROID_EMU_VK_ICD", "swiftshader");
    System::get()->setEnvironmentVariable("ANDROID_EMU_RENDERER", "angle_indirect");
    System::get()->setEnvironmentVariable("ANGLE_DEFAULT_PLATFORM", "swiftshader");
    // lavapipe prebuilt on mac has llvm deps, so it might not work if llvm is not there
    // so only turn lavapipe on if we have -gpu lavapipe
    if (bool gpu_lavapipe = mOpts.gpu && std::string(mOpts.gpu) == "lavapipe"; gpu_lavapipe) {
        System::get()->setEnvironmentVariable("ANDROID_EMU_VK_ICD", "lavapipe");
        System::get()->setEnvironmentVariable("ANGLE_DEFAULT_PLATFORM", "vulkan");
    }
#else
    // ANGLE works fine on mac/windows on top of lavapipe, no need to change it
    // in addition, swiftshader does not work on mac anyway
    System::get()->setEnvironmentVariable("ANDROID_EMU_RENDERER", "angle_indirect");
    System::get()->setEnvironmentVariable("ANGLE_DEFAULT_PLATFORM", "vulkan");
    System::get()->setEnvironmentVariable("ANDROID_EMU_VK_ICD", "lavapipe");
#endif

    // now all default to lavapipe

    if (bool gpu_host = mOpts.gpu && std::string(mOpts.gpu) == "host"; gpu_host) {
      System::get()->setEnvironmentVariable("ANGLE_DEFAULT_PLATFORM", "vulkan");
#if defined(__APPLE__)
      System::get()->setEnvironmentVariable("ANDROID_EMU_VK_ICD", "moltenvk");
#elif defined(__linux__)
      System::get()->setEnvironmentVariable("ANDROID_EMU_VK_ICD", "");
#else
    // TODO windows
#endif
    }

    const std::vector<std::string> args = getCmdline();
    {
        std::vector<std::string> printableArgs(args.size());
        std::transform(args.begin(), args.end(), printableArgs.begin(),
                       [](const std::string& a) -> std::string {
                           if (std::any_of(a.begin(), a.end(),
                                           [](const char c) { return std::isspace(c); })) {
                               using namespace std::literals::string_literals;
                               return "\""s + a + "\""s;
                           } else {
                               return a;
                           }
                       });

        ABSL_LOG(INFO) << "Launch: " << absl::StrJoin(printableArgs, " ");
    }

    auto proc = android::base::Command::create(args).replace().execute();
    // We only get here if we failed to launch the application
    return absl::InternalError(absl::StrFormat("Failed to launch emulator, error code: %d", errno));
}
}  // namespace android::goldfish
