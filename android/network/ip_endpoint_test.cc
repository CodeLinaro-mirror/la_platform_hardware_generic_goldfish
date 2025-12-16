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

#include <gtest/gtest.h>

namespace goldfish::network {

TEST(IpEndpointTest, ToIpEndpoint_4) {
    const Ipv4Endpoint ep4 = ToIpEndpoint(ToIpv4Address(192, 168, 1, 42), 1234);
    EXPECT_EQ(ep4.addr, ToIpv4Address(192, 168, 1, 42));
    EXPECT_EQ(ep4.port, 1234);
}

TEST(IpEndpointTest, ToString_4) {
    EXPECT_EQ(ToString(ToIpEndpoint(ToIpv4Address(192, 168, 1, 42), 1234)), "[192.168.1.42]:1234");
}

TEST(IpEndpointTest, ToSockaddr_4) {
    const struct sockaddr_in sa4 = ToSockaddr(ToIpEndpoint(ToIpv4Address(192, 168, 1, 42), 1234));

    EXPECT_EQ(sa4.sin_family, AF_INET);
    EXPECT_EQ(sa4.sin_addr, ToIpv4Address(192, 168, 1, 42));
    EXPECT_EQ(sa4.sin_port, htons(1234));
}

TEST(IpEndpointTest, equals_4) {
    const Ipv4Endpoint a = ToIpEndpoint(ToIpv4Address(192, 168, 1, 42), 1234);
    const Ipv4Endpoint b = ToIpEndpoint(ToIpv4Address(192, 168, 1, 42), 4321);
    const Ipv4Endpoint c = ToIpEndpoint(ToIpv4Address(192, 168, 1, 43), 1234);

    EXPECT_EQ(a, a);
    EXPECT_NE(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(b, a);
    EXPECT_EQ(b, b);
    EXPECT_NE(b, c);
    EXPECT_NE(c, a);
    EXPECT_NE(c, b);
    EXPECT_EQ(c, c);
}

TEST(IpAddressTest, ToIpEndpoint_6) {
    const struct in6_addr sin6 = *ToIpv6Address("2001:db8:85a3::8a2e:370:7334");
    const Ipv6Endpoint ep6 = ToIpEndpoint(sin6, 1234);
    EXPECT_EQ(ep6.addr, sin6);
    EXPECT_EQ(ep6.port, 1234);
}

TEST(IpEndpointTest, ToString_6) {
    const struct in6_addr sin6 = *ToIpv6Address("2001:db8:85a3::8a2e:370:7334");
    EXPECT_EQ(ToString(ToIpEndpoint(sin6, 1234)), "[2001:db8:85a3::8a2e:370:7334]:1234");
}

TEST(IpEndpointTest, ToSockaddr_6) {
    const struct in6_addr sin6 = *ToIpv6Address("2001:db8:85a3::8a2e:370:7334");
    const struct sockaddr_in6 sa6 = ToSockaddr(ToIpEndpoint(sin6, 1234));

    EXPECT_EQ(sa6.sin6_family, AF_INET6);
    EXPECT_EQ(sa6.sin6_addr, sin6);
    EXPECT_EQ(sa6.sin6_port, htons(1234));
}

TEST(IpEndpointTest, equals_6) {
    const Ipv6Endpoint a = ToIpEndpoint(*ToIpv6Address("2001:db8:85a3::8a2e:370:7334"), 1234);
    const Ipv6Endpoint b = ToIpEndpoint(*ToIpv6Address("2001:db8:85a3::8a2e:370:7334"), 4321);
    const Ipv6Endpoint c = ToIpEndpoint(*ToIpv6Address("2001:db8:85a3::8a2e:370:2222"), 1234);

    EXPECT_EQ(a, a);
    EXPECT_NE(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(b, a);
    EXPECT_EQ(b, b);
    EXPECT_NE(b, c);
    EXPECT_NE(c, a);
    EXPECT_NE(c, b);
    EXPECT_EQ(c, c);
}

}  // namespace goldfish::network