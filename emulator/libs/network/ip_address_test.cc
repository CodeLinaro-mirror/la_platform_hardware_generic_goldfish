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

#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"

#include "android/status/status_matcher_macros.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace goldfish::network {

using absl_testing::IsOkAndHolds;
using absl_testing::StatusIs;
using ::testing::HasSubstr;

TEST(IpAddressTest, ToIpv4Address) {
    const std::string k_ipv4_str("192.168.1.42");

    struct in_addr expected_addr;
    ASSERT_EQ(1, ::inet_pton(AF_INET, k_ipv4_str.c_str(), &expected_addr))
            << "Test setup failed: inet_pton could not parse " << k_ipv4_str;

    EXPECT_EQ(ToString(expected_addr), k_ipv4_str);

    EXPECT_EQ(ToIpv4Address(192, 168, 1, 42), expected_addr);
    EXPECT_THAT(ToIpv4Address(k_ipv4_str.c_str()), IsOkAndHolds(expected_addr));
    EXPECT_THAT(ToIpv4Address(k_ipv4_str), IsOkAndHolds(expected_addr));
}

TEST(IpAddressTest, ToIpv4Address_str_long) {
    std::string ipv4_str("192.168.1.42");

    ipv4_str.resize(INET_ADDRSTRLEN - 1, 0);
    EXPECT_THAT(ToIpv4Address(std::string_view(ipv4_str)),
                IsOkAndHolds(ToIpv4Address(192, 168, 1, 42)));

    ipv4_str.resize(INET_ADDRSTRLEN, 0);
    EXPECT_THAT(ToIpv4Address(std::string_view(ipv4_str)),
                StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("The address is too long")));

    ipv4_str.resize(INET_ADDRSTRLEN + 1, 0);
    EXPECT_THAT(ToIpv4Address(std::string_view(ipv4_str)),
                StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("The address is too long")));
}

TEST(IpAddressTest, ToIpv4Address_invalid) {
    EXPECT_THAT(ToIpv4Address("not an IP address"),
                StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("Invalid IPv4 address")));
}

TEST(IpAddressTest, ToIpv6Address) {
    const std::string k_ipv6_str("2001:db8:85a3::8a2e:370:7334");

    struct in6_addr expected_addr;
    ASSERT_EQ(1, ::inet_pton(AF_INET6, k_ipv6_str.c_str(), &expected_addr))
            << "Test setup failed: inet_pton could not parse " << k_ipv6_str;

    EXPECT_EQ(ToString(expected_addr), k_ipv6_str);

    EXPECT_EQ(ToIpv6Address(0x2001, 0xdb8, 0x85a3, 0, 0, 0x8a2e, 0x370, 0x7334), expected_addr);
    EXPECT_THAT(ToIpv6Address(k_ipv6_str.c_str()), IsOkAndHolds(expected_addr));
    EXPECT_THAT(ToIpv6Address(k_ipv6_str), IsOkAndHolds(expected_addr));
}

TEST(IpAddressTest, ToString_Canonicalization) {
    auto ip = ToIpv6Address("0:0:0:0:0:0:0:1");
    ASSERT_TRUE(ip.ok());
    EXPECT_EQ(ToString(*ip), "::1");
}

TEST(IpAddressTest, ToIpv6Address_str_long) {
    std::string ipv6_str("::1");

    ipv6_str.resize(INET6_ADDRSTRLEN - 1, 0);
    EXPECT_THAT(ToIpv6Address(std::string_view(ipv6_str)), IsOkAndHolds(*ToIpv6Address("::1")));

    ipv6_str.resize(INET6_ADDRSTRLEN, 0);
    EXPECT_THAT(ToIpv6Address(std::string_view(ipv6_str)),
                StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("The address is too long")));

    ipv6_str.resize(INET6_ADDRSTRLEN + 1, 0);
    EXPECT_THAT(ToIpv6Address(std::string_view(ipv6_str)),
                StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("The address is too long")));
}

TEST(IpAddressTest, ToIpv6Address_invalid) {
    EXPECT_THAT(ToIpv6Address("not an IP address"),
                StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("Invalid IPv6 address")));
}

TEST(IpAddressTest, ToIpAddress_str) {
    EXPECT_TRUE(std::holds_alternative<struct in_addr>(*ToIpAddress("127.0.0.1")));
    EXPECT_TRUE(std::holds_alternative<struct in6_addr>(*ToIpAddress("::1")));
    EXPECT_THAT(ToIpAddress("not an IP address"),
                StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("Invalid IP address")));
}

TEST(IpAddressTest, ToIpAddress_sockaddr) {
    struct sockaddr_storage storage = {};

    reinterpret_cast<struct sockaddr_in&>(storage).sin_addr = ToIpv4Address(192, 168, 1, 42);
    storage.ss_family = AF_INET;
    EXPECT_TRUE(std::holds_alternative<struct in_addr>(
            *ToIpAddress(reinterpret_cast<struct sockaddr&>(storage))));

    reinterpret_cast<struct sockaddr_in6&>(storage).sin6_addr = *ToIpv6Address("::1");
    storage.ss_family = AF_INET6;
    EXPECT_TRUE(std::holds_alternative<struct in6_addr>(
            *ToIpAddress(reinterpret_cast<struct sockaddr&>(storage))));

    storage.ss_family = AF_UNSPEC;
    EXPECT_THAT(
            ToIpAddress(reinterpret_cast<struct sockaddr&>(storage)),
            StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("Unexpected address family")));
}

}  // namespace goldfish::network