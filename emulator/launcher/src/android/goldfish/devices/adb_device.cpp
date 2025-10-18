#include "adb_device.h"

#include <vector>

#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/str_cat.h"

#include "android/cmdline-definitions.h"

namespace android::goldfish {

absl::Status AdbDevice::initialize(const EmulatorConfig& emulator) {
    int adbPort = 5555;

    auto opts = emulator.opts();
    if (opts.ports) {
        // Format should be console_port,adb_port
        std::vector<std::string_view> ports = absl::StrSplit(opts.ports, ',');
        if (ports.size() != 2) {
            return absl::InvalidArgumentError(absl::StrCat("Failed to parse ADB port number from -ports: ", opts.ports));
        }
        if (!absl::SimpleAtoi(ports[1], &adbPort)) {
            return absl::InvalidArgumentError(absl::StrCat("Failed to parse ADB port number from -ports: ", opts.ports));
        }
    } else if (opts.port) {
        // opts.port specifies the telnet console port and by default ADB port is that +1
        if (!absl::SimpleAtoi(opts.port, &adbPort)) {
            return absl::InvalidArgumentError(absl::StrCat("Failed to parse ADB port number from -ports ", opts.port));
        } else {
            adbPort += 1;
        }
    }

    mPort = adbPort;
    return absl::OkStatus();
}

std::vector<std::string> AdbDevice::getQemuParameters(const EmulatorConfig& emulator) const {
    std::string_view monitor = emulator.opts().monitor_adb ? ",monitor=true" : "";
    return {"-device", absl::StrCat("virtio-goldfish-adb,host_port=", mPort, monitor)};
}

}  // namespace android::goldfish
