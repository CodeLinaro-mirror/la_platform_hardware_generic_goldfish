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

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#include "absl/status/status_matchers.h"

#include "aemu/base/utils/status_matcher_macros.h"

namespace goldfish::network {

using absl_testing::IsOkAndHolds;
using absl_testing::StatusIs;
using ::testing::AllOf;
// Removed using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsNull;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::Property;

TEST(IpAddressTest, CreateValidIPv4) {
    EXPECT_THAT(IpAddress::Create("127.0.0.1"),
                IsOkAndHolds(AllOf(Property(&IpAddress::ToString, "127.0.0.1"),
                                   Property(&IpAddress::Family, IpAddress::Family::kIpv4))));
}

TEST(IpAddressTest, CreateValidIPv6) {
    ASSERT_OK_AND_ASSIGN(auto ip, IpAddress::Create("::1"));
    EXPECT_EQ("::1", ip.ToString());
    EXPECT_EQ(IpAddress::Family::kIpv6, ip.Family());
}

TEST(IpAddressTest, CreateInvalidHostname) {
    EXPECT_THAT(IpAddress::Create("localhost"),
                StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("Invalid IP address")));
}

TEST(IpAddressTest, CreateInvalidMalformed) {
    EXPECT_THAT(IpAddress::Create("127.0.0"),
                StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("Invalid IP address")));
}

TEST(IpAddressTest, FamilyConvenience) {
    ASSERT_OK_AND_ASSIGN(auto ipv4, IpAddress::Create("192.168.0.1"));
    EXPECT_TRUE(ipv4.IsIpv4());
    EXPECT_FALSE(ipv4.IsIpv6());

    ASSERT_OK_AND_ASSIGN(auto ipv6, IpAddress::Create("2001::1"));
    EXPECT_FALSE(ipv6.IsIpv4());
    EXPECT_TRUE(ipv6.IsIpv6());
}
TEST(IpAddressTest, AsV4ReturnsCorrectBinary) {
    const char* k_ip_v4_str = "192.168.1.100";
    ASSERT_OK_AND_ASSIGN(auto ip, IpAddress::Create(k_ip_v4_str));

    const struct in_addr* v4_addr = ip.AsV4();
    ASSERT_THAT(v4_addr, NotNull());
    EXPECT_THAT(ip.AsV6(), IsNull());

    struct in_addr expected_addr;
    ASSERT_EQ(1, inet_pton(AF_INET, k_ip_v4_str, &expected_addr))
            << "Test setup failed: inet_pton could not parse " << k_ip_v4_str;

    EXPECT_EQ(0, memcmp(v4_addr, &expected_addr, sizeof(expected_addr)));
}

TEST(IpAddressTest, AsV6ReturnsCorrectBinary) {
    const char* k_ip_v6_str = "2001:db8:85a3::8a2e:370:7334";
    ASSERT_OK_AND_ASSIGN(auto ip, IpAddress::Create(k_ip_v6_str));

    const struct in6_addr* v6_addr = ip.AsV6();
    ASSERT_THAT(v6_addr, NotNull());
    EXPECT_THAT(ip.AsV4(), IsNull());

    struct in6_addr expected_addr;
    ASSERT_EQ(1, inet_pton(AF_INET6, k_ip_v6_str, &expected_addr))
            << "Test setup failed: inet_pton could not parse " << k_ip_v6_str;

    EXPECT_EQ(0, memcmp(v6_addr, &expected_addr, sizeof(expected_addr)));
}

TEST(IpAddressTest, AsV6ReturnsCorrectBinaryLoopback) {
    const char* k_ip_v6_str = "::1";
    ASSERT_OK_AND_ASSIGN(auto ip, IpAddress::Create(k_ip_v6_str));

    const struct in6_addr* v6_addr = ip.AsV6();
    ASSERT_THAT(v6_addr, NotNull());
    EXPECT_THAT(ip.AsV4(), IsNull());

    struct in6_addr expected_addr;
    ASSERT_EQ(1, inet_pton(AF_INET6, k_ip_v6_str, &expected_addr))
            << "Test setup failed: inet_pton could not parse " << k_ip_v6_str;

    EXPECT_EQ(0, memcmp(v6_addr, &expected_addr, sizeof(expected_addr)));
}
}  // namespace goldfish::network
