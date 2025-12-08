// Copyright (C) 2025 The Android Open Source Project
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
#include "goldfish/network/ip_address.h"

#include <cstring>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace goldfish::network {

IpAddress::IpAddress(const in_addr* ipv4) : family_(Family::kIpv4) {
    std::memset(addr_.data(), 0, addr_.size());
    std::memcpy(addr_.data(), ipv4, sizeof(struct in_addr));
}

IpAddress::IpAddress(const in6_addr* ipv6) : family_(Family::kIpv6) {
    std::memset(addr_.data(), 0, addr_.size());
    std::memcpy(addr_.data(), ipv6, sizeof(struct in6_addr));
}

absl::StatusOr<IpAddress> IpAddress::Create(std::string_view ip_address) {
    // We need a null-terminated string for inet_pton.
    const std::string ip_str(ip_address);

    // A temporary buffer to hold the binary conversion
    std::array<std::byte, sizeof(struct in6_addr)> buf;

    if (inet_pton(AF_INET, ip_str.c_str(), buf.data()) == 1) {
        return IpAddress(reinterpret_cast<const struct in_addr*>(buf.data()));
    }

    if (inet_pton(AF_INET6, ip_str.c_str(), buf.data()) == 1) {
        return IpAddress(reinterpret_cast<const struct in6_addr*>(buf.data()));
    }

    return absl::InvalidArgumentError(absl::StrFormat("Invalid IP address: %s", ip_str));
}

absl::StatusOr<IpAddress> IpAddress::FromBinary(const struct in_addr* addr) {
    if (addr == nullptr) {
        return absl::InvalidArgumentError("in_addr pointer cannot be null");
    }
    return IpAddress(addr);
}

// --- Factory: fromBinary (IPv6 Overload) ---
absl::StatusOr<IpAddress> IpAddress::FromBinary(const struct in6_addr* addr) {
    if (addr == nullptr) {
        return absl::InvalidArgumentError("in6_addr pointer cannot be null");
    }
    return IpAddress(addr);
}

std::string IpAddress::ToString() const {
    char ip_str[INET6_ADDRSTRLEN];
    const int af = (family_ == Family::kIpv4) ? AF_INET : AF_INET6;

    if (!inet_ntop(af, addr_.data(), ip_str, sizeof(ip_str))) {
        // Note, this cannot happen.
        LOG(FATAL) << "inet_ntop failed, this means the internal ip string got corrupted.";
    }
    return {ip_str};
}

const struct in_addr* IpAddress::AsV4() const {
    if (family_ != Family::kIpv4) {
        return nullptr;
    }

    static_assert(sizeof(addr_) >= sizeof(struct in_addr));
    return reinterpret_cast<const struct in_addr*>(addr_.data());
}

const struct in6_addr* IpAddress::AsV6() const {
    if (family_ != Family::kIpv6) {
        return nullptr;
    }
    static_assert(sizeof(addr_) >= sizeof(struct in6_addr));
    return reinterpret_cast<const struct in6_addr*>(addr_.data());
}

}  // namespace goldfish::network
