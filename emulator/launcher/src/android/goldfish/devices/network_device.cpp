#include "network_device.h"

#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace android::goldfish {

absl::Status NetworkDevice::initialize(const Emulator& emulator) {
    return absl::OkStatus();
}

namespace {
std::string network_device_type(const Avd &avd, std::string_view addr) {
    // virito-net-device on aarch64 vs virtio-net-pci on X86_64
    switch (avd.detectArchitecture()) {
        case Avd::CpuArchitecture::kArm:
          return "virtio-net-device";
        case Avd::CpuArchitecture::kX86:
          return absl::StrCat("virtio-net-pci,addr=", addr);
        case Avd::CpuArchitecture::kRiscV:
        case Avd::CpuArchitecture::kUnknown:
        default:
          // error
          return "";
    }
}
} // namespace

std::vector<std::string> NetworkDevice::getQemuParameters(const Emulator& emulator) const {
    return {
            // First basic ethernet - hubport backend so that slirp isn't
            // required.
            "-netdev", "hubport,id=mynet,hubid=1234",
            "-device", absl::StrCat(network_device_type(emulator.avd(), addr()), ",netdev=mynet"),

            // Second wifi from netsim.
            // Could replace dhcpstart with user options
            // TODO(whollins): Enable this when the virtio-wifi plugin is ready.
            //"-netdev user,id=virtio-wifi,dhcpstart=10.0.2.16",
            //"-device virtio-wifi-pci,netdev=virtio-wifi",
    };
}

}  // namespace android::goldfish

