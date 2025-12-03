#include "wifi_device.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace android::goldfish {

absl::Status WifiDevice::initialize(const EmulatorConfig& emulator) {
    return absl::OkStatus();
}

std::vector<std::string> WifiDevice::getQemuParameters(const EmulatorConfig& emulator) const {
    return {
        "-device",
        absl::StrCat("netsim-netdev,id=wifi,grpc_endpoint=", emulator.netsim_endpoint()),
        "-device",
        absl::StrCat("virtio-wifi-pci,netdev=wifi,addr=", addr(),
                     ",mac_prefix=", emulator.serial_number()),
    };
}

}  // namespace android::goldfish
