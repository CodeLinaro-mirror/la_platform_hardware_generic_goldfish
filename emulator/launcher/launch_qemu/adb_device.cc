#include "adb_device.h"

#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

#include "android/base/system.h"
#include "android/cmdline_definitions.h"

namespace android::goldfish {

absl::Status AdbDevice::initialize(const EmulatorConfig& emulator) {
    // This is picked up by integration tests that try to discover the right emulator.
    LOG(WARNING) << "Expected adb serial number: emulator-" << emulator.serial_number();
    mPort = emulator.adb_port();
    if (mPort <= 0) {
        return absl::InvalidArgumentError(absl::StrCat("adb_port should be > 0: ", mPort));
    }
    return absl::OkStatus();
}

std::vector<std::string> AdbDevice::getQemuParameters(const EmulatorConfig& emulator) const {
    std::string_view monitor = emulator.opts().monitor_adb ? ",monitor=true" : "";
    return {"-device", absl::StrCat("virtio-goldfish-adb,host_port=", mPort, monitor)};
}

absl::flat_hash_map<std::string, std::string> AdbDevice::getQemuEnv(
        const EmulatorConfig& /*emulator*/) const {
    absl::flat_hash_map<std::string, std::string> env;
    if (std::string port = android::base::System::Get()->EnvGet("ANDROID_ADB_SERVER_PORT");
        !port.empty()) {
        env.emplace("ANDROID_ADB_SERVER_PORT", std::move(port));
    }
    return env;
}

}  // namespace android::goldfish
