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

#include <bit>
#include <string>
#include <string_view>
#include <variant>

// clang-format off
// IWYU pragma: begin_keep
#ifdef _WIN32
#include <winsock2.h>
#include <ws2def.h>
#include <ws2ipdef.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif
// IWYU pragma: end_keep
// clang-format on

#ifndef IN6ADDR_LOOPBACK_INIT
#define IN6ADDR_LOOPBACK_INIT                   \
    {                                           \
        { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } } \
    }
#endif

#include "absl/status/statusor.h"

namespace goldfish::network {

inline constexpr struct in_addr ToIpv4Address(const uint8_t a, const uint8_t b, const uint8_t c,
                                              const uint8_t d) {
    static_assert(sizeof(struct in_addr) == 4);
    const uint8_t bits[4] = {a, b, c, d};
    return std::bit_cast<struct in_addr>(bits);
}

absl::StatusOr<struct in_addr> ToIpv4Address(const char* ip_address);
absl::StatusOr<struct in_addr> ToIpv4Address(const std::string& ip_address);
absl::StatusOr<struct in_addr> ToIpv4Address(std::string_view ip_address);
#ifdef _WIN32
inline constexpr struct in_addr kIPv4LoopbackAddress = {.S_un = {.S_addr = 0x0100007f}};
#else
inline constexpr struct in_addr kIPv4LoopbackAddress = ToIpv4Address(127, 0, 0, 1);
#endif

std::string ToString(struct in_addr);

struct in6_addr ToIpv6Address(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e,
                              uint16_t f, uint16_t g, uint16_t h);

absl::StatusOr<struct in6_addr> ToIpv6Address(const char* ip_address);
absl::StatusOr<struct in6_addr> ToIpv6Address(const std::string& ip_address);
absl::StatusOr<struct in6_addr> ToIpv6Address(std::string_view ip_address);
inline constexpr struct in6_addr kIPv6LoopbackAddress = IN6ADDR_LOOPBACK_INIT;

std::string ToString(const struct in6_addr&);

using IpAddress = std::variant<struct in_addr, struct in6_addr>;

absl::StatusOr<IpAddress> ToIpAddress(const char* ip_address);
absl::StatusOr<IpAddress> ToIpAddress(const std::string& ip_address);
absl::StatusOr<IpAddress> ToIpAddress(std::string_view ip_address);
absl::StatusOr<IpAddress> ToIpAddress(const struct sockaddr&);

std::string ToString(const IpAddress&);

}  // namespace goldfish::network

inline bool operator==(const in_addr lhs, const in_addr rhs) {
    return lhs.s_addr == rhs.s_addr;
}

inline bool operator==(const in6_addr& lhs, const in6_addr& rhs) {
    return !::memcmp(&lhs, &rhs, sizeof(lhs));
}
