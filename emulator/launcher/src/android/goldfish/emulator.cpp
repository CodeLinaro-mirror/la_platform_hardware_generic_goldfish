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
#include <chrono>
#include <filesystem>
#include <future>
#include <initializer_list>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

// Use ABSL_LOG to avoid conflict with crashpadh logging
#include "absl/log/absl_log.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

#include "aemu/base/files/IniFile.h"
#include "aemu/base/network/Dns.h"
#include "aemu/base/network/IpAddress.h"
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
#include "devices/wifi_device.h"

namespace android::goldfish {

using android::base::Bazel;
using android::base::System;

namespace {

std::unique_ptr<android::base::ObservableProcess> RunNetsimd(const fs::path& netsim_binary,
                                                             const AndroidOptions& opts) {
    bool no_cli_ui = true;  //! feature_is_enabled(kFeature_NetsimCliUi),
    bool no_web_ui = true;  //! feature_is_enabled(kFeature_NetsimWebUi),
    std::string host_dns = opts.dns_server ? opts.dns_server : "";
    if (host_dns.empty()) {
        android::base::Dns::AddressList al = android::base::Dns::getSystemServerList();
        host_dns = absl::StrJoin(al, ",", [](std::string* out, const android::base::IpAddress& ip) {
            absl::StrAppend(out, ip.toString());
        });
        VLOG(1) << "Netsim DNS set to: " << host_dns;
    }
    std::string_view http_proxy = opts.http_proxy ? opts.http_proxy : "";
    std::string_view netsim_args = opts.netsim_args ? opts.netsim_args : "";

    std::vector<std::string> program_with_args{netsim_binary.string()};
    if (no_cli_ui) {
        program_with_args.push_back("--no-cli-ui");
    }
    if (no_web_ui) {
        program_with_args.push_back("--no-web-ui");
    }
    if (!host_dns.empty()) {
        program_with_args.push_back(absl::StrCat("--host-dns=", host_dns));
    }
    if (!http_proxy.empty()) {
        program_with_args.push_back(absl::StrCat("--http-proxy=", http_proxy));
    }

    for (auto& flag : absl::StrSplit(netsim_args, " ", absl::SkipEmpty())) {
        program_with_args.push_back(std::string(flag));
    }

    LOG(INFO) << "Netsimd launch command:" << absl::StrJoin(program_with_args, " ");
    auto cmd = android::base::Command::create(program_with_args);
    // TODO(whollins): It would be useful to see stderr and stdout for debugging.
    auto netsimd = cmd.asDeamon().execute();
    if (netsimd) {
        LOG(INFO) << "Running netsimd as pid: " << netsimd->pid();
    }

    return netsimd;
}

}  // namespace

absl::Status Emulator::addDevices() {
    // Device are initialized in order of appearance
    // So if device B depends on device A, you should register them as:
    // -device A -device B ...
    int pluginLogLevel = static_cast<int>(mOpts.verbose ? absl::LogSeverityAtLeast::kInfo : absl::LogSeverityAtLeast::kWarning);

    std::string vmodules = mOpts.vmodule ? mOpts.vmodule : "";
    if (System::get()->getEnvironmentVariable("AEMU_LOG_LEVEL").empty()) {
        System::get()->setEnvironmentVariable("AEMU_LOG_LEVEL", absl::StrCat(pluginLogLevel));
    }
    if (System::get()->getEnvironmentVariable("AEMU_VLOG_LEVEL").empty()) {
        System::get()->setEnvironmentVariable("AEMU_VLOG_LEVEL", absl::StrCat(mOpts.V ? mOpts.V : ""));
    }
    if (System::get()->getEnvironmentVariable("AEMU_VMODULE").empty()) {
        System::get()->setEnvironmentVariable("AEMU_VMODULE", vmodules);
    }

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

    // No ethernet device for now:
    // addDevice<NetworkDevice>("0a.0");

    if (!mOpts.no_netsim && !mOpts.no_wifi) {
      addDevice<WifiDevice>("0b.0");
    }

    // Hardware RNG device
    addDevice<ParameterList>(std::initializer_list<std::string>{
        "-device", "virtio-rng-pci",
    });

    // This is needed for virtconsole (logcat, bt, uwb).
    // TODO old emulator also created a virtio-serial device, do we need to?
    addDevice<ParameterList>(std::initializer_list<std::string>{
        "-device", "virtio-serial-pci,ioeventfd=off",
    });

    // virtio logcat consoles, note that order matters here!
    // This device is probably just to make sure that logcat is on device 1 and not 0.
    addDevice<ParameterList>(std::initializer_list<std::string>{
        "-device", "virtconsole,chardev=forhvc0,name=logcat_null", "-chardev", "null,id=forhvc0",
    });
    if (mOpts.logcat_output) {
        // virtio logcat consoles, note that order matters here!
        addDevice<ParameterList>(std::initializer_list<std::string>{
            // Actual logcat location.
            "-device", "virtconsole,chardev=forhvc1,name=logcat", "-chardev",
            absl::StrCat("file,id=forhvc1,path=", mOpts.logcat_output)});
    } else {
        addDevice<ParameterList>(std::initializer_list<std::string>{
            // Actual logcat location.
            "-device", "virtconsole,chardev=forhvc1,name=logcat", "-chardev", "null,id=forhvc1"});
    }

    if (!mOpts.no_netsim) {
        // The name of these vport devices should be used by http://ac/device/generic/goldfish/qemu-props/vport_parser.cpp
        // It should lookup the actual port number and set the property "vendor.qemu.vport.<name>" to "/dev/vport8p<N>"
        // /dev/vport8p3 for bt (4th port)
        // TODO(b/450338546): this isn't currently working and instead there is a hack in avd-info.cpp to workaround.
        addDevice<ParameterList>(std::initializer_list<std::string>{
            "-chardev", absl::StrCat("netsim-uwb,id=uwb,host=", netsim_endpoint()),
            "-device", "virtconsole,chardev=uwb,name=uwb",

            "-chardev", absl::StrCat("netsim-bt,id=bluetooth,host=", netsim_endpoint()),
            "-device", "virtserialport,chardev=bluetooth,name=bluetooth",
        });
    }

    if (mOpts.show_kernel) {
        addDevice<ParameterList>(std::initializer_list<std::string>{"-serial", "stdio"});
    }

    auto ini_path = System::pathAsString(mAvd->getIniFile());
    std::string avd_params = absl::StrCat("ini_path=", ini_path, ",serial_number=", serial_number());
    if (mOpts.quit_after_boot) {
        if (int timeout; absl::SimpleAtoi(mOpts.quit_after_boot, &timeout)) {
            absl::StrAppend(&avd_params, ",quit_after_boot_timeout=", timeout);
        } else {
            return absl::InvalidArgumentError(absl::StrCat("Failed to parse -quit-after-boot parameter as int: ", mOpts.quit_after_boot));
        }
    }
    addDevice<ParameterList>(std::initializer_list<std::string>{
        "-device",
        absl::StrCat("avdstart,", avd_params),
    });

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

fs::path getEnvDir(const char* envvar, std::string_view subdir) {
    if (char* env_p = std::getenv(envvar); env_p && *env_p) {
        return fs::path(env_p) / subdir;
    }
    LOG(WARNING) << "No discovery env for " << envvar << ", using tmp/";
    return fs::path("/tmp");
}

fs::path GetNetsimDiscoveryDir() {
    // $TMPDIR is the temp directory on buildbots (and Mac).
    const char* test_env_p = std::getenv("TMPDIR");
    if (test_env_p && *test_env_p) {
        return fs::path(test_env_p);
    }
#if defined(_WIN32)
    return getEnvDir("LOCALAPPDATA", "Temp");
#elif defined(__linux__)
    return getEnvDir("XDG_RUNTIME_DIR", "");
#elif defined(__APPLE__)
    return getEnvDir("HOME", "Library/Caches/TemporaryItems");
#else
#error This platform is not supported.
#endif
}

int read_netsim_port() {
    // TODO(whollins): Resolve this path with the others in launcher.cpp.
    // IniFile netsim_ini(mResolvedPaths.discovery_directory.parent_path().parent_path() /
    // "netsim.ini");
    IniFile netsim_ini(GetNetsimDiscoveryDir() / "netsim.ini");
    if (!netsim_ini.read()) {
        VLOG(1) << "Failed to read netsim.ini";
        return 0;
    }
    return netsim_ini.getInt("grpc.port", 0);
}

absl::Status Emulator::launch_netsim() {
    std::unique_ptr<android::base::ObservableProcess> netsimd;
    if (auto netsim_endpoint = opts().packet_streamer_endpoint; netsim_endpoint) {
        mNetsimEndpoint = netsim_endpoint;
    } else {
        int existing_port = read_netsim_port();
        if (existing_port != 0) {
            LOG(WARNING) << "netsim.ini already exists with a valid port - either previous netsimd still running or it died without cleanup";
        }
        // netsimd itself will check whether it's already running and exit if so.
        netsimd = RunNetsimd(mResolvedPaths.netsim_binary, opts());
        if (!netsimd->isAlive()) {
            return absl::InternalError("netsimd failed to start");
        }
        // Try to wait in case there was another one running.
        if (netsimd->wait_for(std::chrono::seconds(2)) == std::future_status::ready) {
            LOG(WARNING) << "netsimd died, perhaps another was already running";
            if (existing_port != 0) {
                mNetsimEndpoint = absl::StrCat("localhost:", existing_port);
                return absl::OkStatus();
            } else {
                return absl::InternalError("netsimd died and there was no existing port to connect to");
            }
        }
        netsimd->detach();

        for (int i = 0; i < 10; i++) {
            int port = read_netsim_port();
            if (port == 0) {
                VLOG(1) << "netsimd: Port not yet available";
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            // We expect the port to change, if it doesn't then something strange has happened.
            if (port == existing_port) {
                VLOG(1) << "netsimd: Port in ini file has not yet changed: " << port;
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            VLOG(1) << "netsim.ini parsed successfully, grpc.port set to: " << port;
            mNetsimEndpoint = absl::StrCat("localhost:", port);
            break;
        }
        if (mNetsimEndpoint.empty()) {
            return absl::NotFoundError("Unable to determine the correct grpc endpoint for netsimd");
        }
    }
    return absl::OkStatus();
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
#else
    // ANGLE works fine on mac/windows on top of lavapipe, no need to change it
    // in addition, swiftshader does not work on mac anyway
    System::get()->setEnvironmentVariable("ANDROID_EMU_RENDERER", "angle_indirect");
    System::get()->setEnvironmentVariable("ANGLE_DEFAULT_PLATFORM", "vulkan");
#endif

    // now all default to lavapipe
    System::get()->setEnvironmentVariable("ANDROID_EMU_VK_ICD", "lavapipe");

    if (bool gpu_host = mOpts.gpu && std::string(mOpts.gpu) == "host"; gpu_host) {
      System::get()->setEnvironmentVariable("ANGLE_DEFAULT_PLATFORM", "vulkan");
#if defined(__APPLE__)
      System::get()->setEnvironmentVariable("ANDROID_EMU_VK_ICD", "moltenvk");
#else
      System::get()->setEnvironmentVariable("ANDROID_EMU_VK_ICD", "");
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
