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
#include "goldfish/network/endpoint.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "goldfish/network/ip_address.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#include "aemu/base/utils/status_matcher_macros.h"

namespace goldfish::network {

TEST(EndpointTest, CreateAndAccess) {
    ASSERT_OK_AND_ASSIGN(auto ip, IpAddress::create("127.0.0.1"));
    auto endpoint = Endpoint(ip, 8080);

    EXPECT_EQ("127.0.0.1", endpoint.address().toString());
    EXPECT_EQ(IpAddress::Family::kIpv4, endpoint.address().family());
    EXPECT_EQ(8080, endpoint.port());
}

TEST(EndpointTest, ToStringIPv4) {
    ASSERT_OK_AND_ASSIGN(auto endpoint, Endpoint::create("192.168.1.1", 1234));
    EXPECT_EQ("192.168.1.1:1234", endpoint.toString());
}

TEST(EndpointTest, ToStringIPv6) {
    ASSERT_OK_AND_ASSIGN(auto endpoint, Endpoint::create("::2", 5678));
    EXPECT_EQ("[::2]:5678", endpoint.toString());
}

TEST(EndpointTest, ToSockaddrIPv4) {
    ASSERT_OK_AND_ASSIGN(auto endpoint, Endpoint::create("192.168.1.2", 8080));
    sockaddr_storage addr = endpoint.toSockaddr();

    ASSERT_EQ(AF_INET, addr.ss_family);
    auto* sin = reinterpret_cast<sockaddr_in*>(&addr);
    EXPECT_EQ(8080, ntohs(sin->sin_port));

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &sin->sin_addr, ip_str, sizeof(ip_str));
    EXPECT_STREQ("192.168.1.2", ip_str);
}

TEST(EndpointTest, ToSockaddrIPv6) {
    ASSERT_OK_AND_ASSIGN(auto endpoint, Endpoint::create("2001:db8::1", 12345));
    sockaddr_storage addr = endpoint.toSockaddr();

    ASSERT_EQ(AF_INET6, addr.ss_family);
    auto* sin6 = reinterpret_cast<sockaddr_in6*>(&addr);
    EXPECT_EQ(12345, ntohs(sin6->sin6_port));

    char ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &sin6->sin6_addr, ip_str, sizeof(ip_str));
    EXPECT_STREQ("2001:db8::1", ip_str);
}

}  // namespace goldfish::network