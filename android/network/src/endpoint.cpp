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
#include "goldfish/network/endpoint.h"

#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"

#include "goldfish/network/ip_address.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

namespace goldfish::network {

Endpoint::Endpoint(IpAddress ip_address, int port)
        : mIpAddress(std::move(ip_address)), mPort(port) {}

absl::StatusOr<Endpoint> Endpoint::create(std::string_view ip_address, int port) {
    auto ip = IpAddress::create(ip_address);
    if (!ip.ok()) {
        return ip.status();
    }
    return Endpoint(std::move(*ip), port);
}

std::string Endpoint::toString() const {
    if (mIpAddress.family() == IpAddress::Family::kIpv6) {
        return absl::StrFormat("[%s]:%d", mIpAddress.toString(), mPort);
    }
    return absl::StrFormat("%s:%d", mIpAddress.toString(), mPort);
}

sockaddr_storage Endpoint::toSockaddr() const {
    sockaddr_storage addr{};

    switch (mIpAddress.family()) {
    case IpAddress::Family::kIpv4: {
        addr.ss_family = AF_INET;
        auto* sin = reinterpret_cast<sockaddr_in*>(&addr);
        sin->sin_port = htons(mPort);
        inet_pton(AF_INET, mIpAddress.toString().c_str(), &sin->sin_addr);
        break;
    }
    case IpAddress::Family::kIpv6: {
        addr.ss_family = AF_INET6;
        auto* sin6 = reinterpret_cast<sockaddr_in6*>(&addr);
        sin6->sin6_port = htons(mPort);
        inet_pton(AF_INET6, mIpAddress.toString().c_str(), &sin6->sin6_addr);
        break;
    }
    }

    return addr;
}

absl::StatusOr<Endpoint> Endpoint::fromSockAddr(const struct sockaddr* sa) {
    if (sa == nullptr) {
        return absl::InvalidArgumentError("sockaddr is null");
    }

    absl::StatusOr<IpAddress> ip;
    int port = 0;

    switch (sa->sa_family) {
    case AF_INET: {
        const auto* sa_in = reinterpret_cast<const struct sockaddr_in*>(sa);
        port = ntohs(sa_in->sin_port);
        ip = IpAddress::fromBinary(&sa_in->sin_addr);
        break;
    }

    case AF_INET6: {
        const auto* sa_in6 = reinterpret_cast<const struct sockaddr_in6*>(sa);
        port = ntohs(sa_in6->sin6_port);
        ip = IpAddress::fromBinary(&sa_in6->sin6_addr);
        break;
    }

    default:
        // Adding the family number to the error is helpful for debugging
        return absl::InvalidArgumentError(
                absl::StrFormat("Unsupported address family: %d", sa->sa_family));
    }

    if (!ip.ok()) {
        return ip.status();
    }

    return Endpoint(std::move(*ip), port);
}

}  // namespace goldfish::network
