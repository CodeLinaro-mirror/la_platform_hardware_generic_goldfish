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
#include "goldfish/network/ip_endpoint.h"

#include "absl/strings/str_cat.h"

namespace goldfish::network {

Ipv4Endpoint ToIpEndpoint(in_addr addr, uint16_t port) {
    return {
        .addr = addr,
        .port = port,
    };
}

Ipv4Endpoint ToIpEndpoint(const struct sockaddr_in& sin4) {
    assert(sin4.sin_family == AF_INET);
    return ToIpEndpoint(sin4.sin_addr, ntohs(sin4.sin_port));
}

std::string ToString(const Ipv4Endpoint& endpoint) {
    return absl::StrCat("[", ToString(endpoint.addr), "]:", endpoint.port);
}

struct sockaddr_in ToSockaddr(const Ipv4Endpoint& ep) {
    struct sockaddr_in sin4 = {};
    sin4.sin_family = AF_INET;
    sin4.sin_addr = ep.addr;
    sin4.sin_port = htons(ep.port);
    return sin4;
}

Ipv6Endpoint ToIpEndpoint(const in6_addr& addr, uint16_t port) {
    return {
        .addr = addr,
        .port = port,
    };
}

Ipv6Endpoint ToIpEndpoint(const struct sockaddr_in6& sin6) {
    assert(sin6.sin6_family == AF_INET6);
    return ToIpEndpoint(sin6.sin6_addr, ntohs(sin6.sin6_port));
}

std::string ToString(const Ipv6Endpoint& endpoint) {
    return absl::StrCat("[", ToString(endpoint.addr), "]:", endpoint.port);
}

struct sockaddr_in6 ToSockaddr(const Ipv6Endpoint& ep) {
    struct sockaddr_in6 sin6 = {};
    sin6.sin6_family = AF_INET6;
    sin6.sin6_addr = ep.addr;
    sin6.sin6_port = htons(ep.port);
    return sin6;
}

}  // namespace goldfish::network
