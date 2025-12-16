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
#pragma once

#include <string>

// clang-format off
// IWYU pragma: begin_keep
#ifdef _WIN32
#include <winsock2.h>
#include <ws2def.h>
#else
#include <sys/socket.h>
#endif
// IWYU pragma: end_keep
// clang-format on

#include "goldfish/network/ip_address.h"

namespace goldfish::network {

struct Ipv4Endpoint {
    in_addr addr = {};
    uint16_t port = 0;  // host endian
};

Ipv4Endpoint ToIpEndpoint(struct in_addr addr, uint16_t port);
Ipv4Endpoint ToIpEndpoint(const struct sockaddr_in&);
std::string ToString(const Ipv4Endpoint&);
struct sockaddr_in ToSockaddr(const Ipv4Endpoint&);

struct Ipv6Endpoint {
    in6_addr addr = {};
    uint16_t port = 0;  // host endian
};

Ipv6Endpoint ToIpEndpoint(const struct in6_addr& addr, uint16_t port);
Ipv6Endpoint ToIpEndpoint(const struct sockaddr_in6&);
std::string ToString(const Ipv6Endpoint&);
struct sockaddr_in6 ToSockaddr(const Ipv6Endpoint&);

inline bool operator==(const Ipv4Endpoint& lhs, const Ipv4Endpoint& rhs) {
    return (lhs.addr == rhs.addr) && (lhs.port == rhs.port);
}

inline bool operator==(const Ipv6Endpoint& lhs, const Ipv6Endpoint& rhs) {
    return (lhs.addr == rhs.addr) && (lhs.port == rhs.port);
}

}  // namespace goldfish::network
