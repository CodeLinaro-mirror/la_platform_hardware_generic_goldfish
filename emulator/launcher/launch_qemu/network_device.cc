#include "network_device.h"

#include <string_view>

#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"

#include "android/base/system.h"
#include "goldfish/network/dns_resolver.h"

namespace android::goldfish {
namespace {

std::string generate_random_mac() {
    absl::BitGen bitgen;

    return absl::StrFormat("52:54:00:%02x:%02x:%02x", absl::Uniform(bitgen, 0, 256),
                           absl::Uniform(bitgen, 0, 256), absl::Uniform(bitgen, 0, 256));
}

}  // namespace

NetworkDevice::NetworkDevice(std::string id, std::string addr, bool cellular, bool netsim_backend)
        : PciDevice(std::move(id), std::move(addr))
        , cellular_(cellular)
        , netsim_backend_(netsim_backend)
        , mac_address_(generate_random_mac()) {}

absl::Status NetworkDevice::initialize(const EmulatorConfig& emulator) {
    if (netsim_backend_) {
        return absl::OkStatus();
    }
    const auto& opts = emulator.opts();
    host_dns_ = opts.dns_server ? opts.dns_server : "";
    if (host_dns_.empty()) {
        if (auto al = ::goldfish::network::GetSystemDnsServers(); al.ok()) {
            host_dns_ = absl::StrJoin(*al, ",", [](std::string* out, const auto& ip) {
                absl::StrAppend(out, ::goldfish::network::ToString(ip));
            });
        } else {
            LOG(WARNING) << "Failed to retrieve the system DNS servers due to: " << al.status();
            LOG(WARNING) << "slirp networking will run with reduced functionality.";
        }
    }
    if (!host_dns_.empty()) {
        VLOG(1) << "Slirp DNS set to: " << host_dns_;
    }
    return absl::OkStatus();
}

namespace {
std::string network_device_type(const Avd& avd, std::string_view addr) {
    // virito-net-device on aarch64 vs virtio-net-pci on X86_64
    switch (avd.Arch()) {
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
    ret.emplace_back(absl::StrCat(network_device_type(emulator.avd(), addr()), ",netdev=", id(),
                                  ",mac=", mac_address_));

    // TODO debug dump packets to file
    // ret.emplace_back("-object");
    // ret.emplace_back(absl::StrCat("filter-dump,id=f1,netdev=", id(), ",file=/tmp/netdump.dat"));

    return ret;
}

absl::flat_hash_map<std::string, std::string> NetworkDevice::getQemuEnv(
        const EmulatorConfig& /*emulator*/) const {
    if (!netsim_backend_ && !host_dns_.empty()) {
        return {{"SLIRP_DNS_SERVERS", host_dns_}};
    }
    return {};
}

}  // namespace android::goldfish
