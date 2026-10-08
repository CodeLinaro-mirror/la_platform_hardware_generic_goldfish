// Copyright 2026 The Android Open Source Project
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

#include "env_passthrough_device.h"

#include <string>
#include <string_view>
#include <utility>

#include "absl/container/flat_hash_map.h"

#include "android/base/system.h"

namespace android::goldfish {
namespace {

constexpr std::string_view kAllowedEnvVars[] = {
    // Used by System::GetHomeDirectory() in the plugin (e.g., TelnetAuth for
    // ~/.emulator_console_auth_token) and by PulseAudio on Linux.
    "HOME",
    "HOMEDRIVE",
    "HOMEPATH",

    // Used by System::GetTempDir() in the plugin (/tmp/android-$USER for
    // CrashSystem and StreetViewUtils).
    "USER",

    // Used by System::AddLibrarySearchDir() (e.g., RendererVulkan) and the OS
    // dynamic linker when loading shared libraries and Vulkan ICDs.
    "PATH",
    "LD_LIBRARY_PATH",
    "DYLD_LIBRARY_PATH",

    // Required on Windows by Winsock (WSAStartup fails with WSASYSNOTREADY if
    // SystemRoot is missing from a custom environment block) and Win32 DLLs.
    "SystemRoot",
    "WINDIR",

    // Used by std::filesystem::temp_directory_path(), GLib (g_get_tmp_dir()),
    // and QEMU (get_tmp_filename()) for temporary backing files.
    "TMPDIR",
    "TEMP",
    "TMP",

    // Used by PulseAudio / PipeWire client libraries in QEMU's audio backend
    // on Linux to locate the user audio socket and authentication cookie.
    "XDG_RUNTIME_DIR",
    "PULSE_SERVER",
    "PULSE_COOKIE",
};

}  // namespace

absl::flat_hash_map<std::string, std::string> EnvPassthroughDevice::getQemuEnv(
        const EmulatorConfig& /*emulator*/) const {
    absl::flat_hash_map<std::string, std::string> env;
    auto* sys = android::base::System::Get();
    for (std::string_view var : kAllowedEnvVars) {
        if (std::string val = sys->EnvGet(var); !val.empty()) {
            env.emplace(var, std::move(val));
        }
    }
    return env;
}

}  // namespace android::goldfish
