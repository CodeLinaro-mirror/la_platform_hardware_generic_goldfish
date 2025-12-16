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

namespace goldfish::network {

absl::StatusOr<Endpoint> ToEndpoint(const struct sockaddr& addr) {
    switch (addr.sa_family) {
    case AF_INET:
        return Endpoint(ToIpEndpoint(reinterpret_cast<const struct sockaddr_in&>(addr)));
    case AF_INET6:
        return Endpoint(ToIpEndpoint(reinterpret_cast<const struct sockaddr_in6&>(addr)));
    case AF_UNIX:
        return Endpoint(ToUnEndpoint(reinterpret_cast<const struct sockaddr_un&>(addr)));
    default:
        return absl::InvalidArgumentError(
                absl::StrFormat("Unsupported address family: %d", addr.sa_family));
    }
}

Endpoint ToEndpoint(const IpAddress& addr, const uint16_t port) {
    return std::visit([port](const auto& addr) { return Endpoint(ToIpEndpoint(addr, port)); },
                      addr);
}

std::string ToString(const Endpoint& endpoint) {
    return std::visit([](const auto& endpoint) { return ToString(endpoint); }, endpoint);
}

struct sockaddr_storage ToSockaddr(const Endpoint& endpoint) {
    return std::visit(
            [](const auto& endpoint) {
                const auto specific_addr = ToSockaddr(endpoint);
                struct sockaddr_storage storage = {};
                static_assert(sizeof(specific_addr) <= sizeof(storage));
                ::memcpy(&storage, &specific_addr, sizeof(specific_addr));
                return storage;
            },
            endpoint);
}

}  // namespace goldfish::network
