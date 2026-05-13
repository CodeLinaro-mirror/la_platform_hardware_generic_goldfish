#include "network_device.h"

#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace android::goldfish {

absl::Status NetworkDevice::initialize(const EmulatorConfig& emulator) {
    return absl::OkStatus();
}

namespace {
std::string network_device_type(const Avd& avd, std::string_view addr) {
    // virito-net-device on aarch64 vs virtio-net-pci on X86_64
    switch (avd.DetectArchitecture()) {
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
}  // namespace

std::vector<std::string> NetworkDevice::getQemuParameters(const EmulatorConfig& emulator) const {
    std::vector<std::string> ret;
    if (netsim_backend_) {
        ret.emplace_back("-device");
        ret.emplace_back(absl::StrCat("netsim-netdev,id=", id(),
                                      ",mode=", cellular_ ? "cellular" : "ethernet"));
    } else {
        ret.emplace_back("-netdev");
        ret.emplace_back(absl::StrCat("user,id=", id()));
    }
    ret.emplace_back("-device");
    ret.emplace_back(absl::StrCat(network_device_type(emulator.avd(), addr()), ",netdev=", id()));

    // TODO debug dump packets to file
    // ret.emplace_back("-object");
    // ret.emplace_back(absl::StrCat("filter-dump,id=f1,netdev=", id(), ",file=/tmp/netdump.dat"));

    return ret;
}

}  // namespace android::goldfish
