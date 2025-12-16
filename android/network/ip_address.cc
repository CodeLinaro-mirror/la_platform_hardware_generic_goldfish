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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "goldfish/network/ip_address.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include "absl/log/log.h"

namespace goldfish::network {
namespace {
std::string IpToStringImpl(const int af, const void* addr) {
    char ip_str[INET6_ADDRSTRLEN];
    if (!inet_ntop(af, addr, ip_str, sizeof(ip_str))) {
        LOG(FATAL) << "inet_ntop failed, this means the internal ip string got corrupted.";
    }
    return ip_str;
}

absl::Status MakeAddressTooLongError(const std::string_view ip_address) {
    return absl::InvalidArgumentError(absl::StrFormat("The address is too long: '%s'", ip_address));
}

absl::Status MakeInvalidArgumentError(const char* family, const char* ip_address) {
    return absl::InvalidArgumentError(
            absl::StrFormat("Invalid %s address: '%s'", family, ip_address));
}
}  // namespace

absl::StatusOr<struct in_addr> ToIpv4Address(const char* ip_address) {
    struct in_addr addr;
    if (::inet_pton(AF_INET, ip_address, &addr) == 1) {
        return addr;
    }

    return MakeInvalidArgumentError("IPv4", ip_address);
}

absl::StatusOr<struct in_addr> ToIpv4Address(const std::string& ip_address) {
    return ToIpv4Address(ip_address.c_str());
}

absl::StatusOr<struct in_addr> ToIpv4Address(const std::string_view ip_address) {
    char ip_str[INET_ADDRSTRLEN];
    if (ip_address.size() >= sizeof(ip_str)) {
        return MakeAddressTooLongError(ip_address);
    }

    ::memcpy(ip_str, ip_address.data(), ip_address.size());
    ip_str[ip_address.size()] = 0;
    return ToIpv4Address(ip_str);
}

std::string ToString(const struct in_addr addr) {
    return IpToStringImpl(AF_INET, &addr);
}

struct in6_addr ToIpv6Address(const uint16_t a, const uint16_t b, const uint16_t c,
                              const uint16_t d, const uint16_t e, const uint16_t f,
                              const uint16_t g, const uint16_t h) {
    static_assert(sizeof(struct in6_addr) == 16);
    const uint16_t bits[8] = {htons(a), htons(b), htons(c), htons(d),
                              htons(e), htons(f), htons(g), htons(h)};
    return std::bit_cast<struct in6_addr>(bits);
}

absl::StatusOr<struct in6_addr> ToIpv6Address(const char* ip_address) {
    struct in6_addr addr;
    if (::inet_pton(AF_INET6, ip_address, &addr) == 1) {
        return addr;
    }

    return MakeInvalidArgumentError("IPv6", ip_address);
}

absl::StatusOr<struct in6_addr> ToIpv6Address(const std::string& ip_address) {
    return ToIpv6Address(ip_address.c_str());
}

absl::StatusOr<struct in6_addr> ToIpv6Address(const std::string_view ip_address) {
    char ip_str[INET6_ADDRSTRLEN];
    if (ip_address.size() >= sizeof(ip_str)) {
        return MakeAddressTooLongError(ip_address);
    }

    ::memcpy(ip_str, ip_address.data(), ip_address.size());
    ip_str[ip_address.size()] = 0;
    return ToIpv6Address(ip_str);
}

std::string ToString(const struct in6_addr& addr) {
    return IpToStringImpl(AF_INET6, &addr);
}

absl::StatusOr<IpAddress> ToIpAddress(const char* ip_address_str) {
    if (auto a = ToIpv4Address(ip_address_str); a.ok()) {
        return *std::move(a);
    }

    if (auto a = ToIpv6Address(ip_address_str); a.ok()) {
        return *std::move(a);
    }

    return MakeInvalidArgumentError("IP", ip_address_str);
}

absl::StatusOr<IpAddress> ToIpAddress(const std::string& ip_address) {
    return ToIpAddress(ip_address.c_str());
}

absl::StatusOr<IpAddress> ToIpAddress(const std::string_view ip_address) {
    char ip_str[INET6_ADDRSTRLEN];
    if (ip_address.size() >= sizeof(ip_str)) {
        return MakeAddressTooLongError(ip_address);
    }

    ::memcpy(ip_str, ip_address.data(), ip_address.size());
    ip_str[ip_address.size()] = 0;
    return ToIpAddress(ip_str);
}

std::string ToString(const IpAddress& addr) {
    return std::visit([](const auto& addr) { return ToString(addr); }, addr);
}

absl::StatusOr<IpAddress> ToIpAddress(const struct sockaddr& addr) {
    switch (addr.sa_family) {
    case AF_INET:
        return IpAddress(reinterpret_cast<const struct sockaddr_in&>(addr).sin_addr);
    case AF_INET6:
        return IpAddress(reinterpret_cast<const struct sockaddr_in6&>(addr).sin6_addr);
    default:
        return absl::InvalidArgumentError(
                absl::StrFormat("Unexpected address family: %d", addr.sa_family));
    }
}

}  // namespace goldfish::network
