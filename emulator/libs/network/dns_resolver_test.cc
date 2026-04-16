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

#include "goldfish/network/dns_resolver.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/log/log.h"
#include "absl/status/status_matchers.h"

#include "android/base/testing/needs_winsock.h"
#include "android/status/status_matcher_macros.h"
#include "goldfish/network/endpoint.h"

namespace goldfish::network {

using absl_testing::StatusIs;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;

TEST(DnsResolverTest, ResolveEndpointsLocalhost) {
    ASSERT_OK_AND_ASSIGN(auto endpoints, ResolveEndpoints("localhost:8080"));
    EXPECT_GT(endpoints.size(), 0);
    for (const auto& endpoint : endpoints) {
        const std::string endroid_str = ToString(endpoint);
        EXPECT_TRUE(endroid_str == "[127.0.0.1]:8080" || endroid_str == "[::1]:8080");
    }
}

TEST(DnsResolverTest, ResolveEndpointsIPv4) {
    ASSERT_OK_AND_ASSIGN(auto endpoints, ResolveEndpoints("127.0.0.1:1234"));
    ASSERT_EQ(endpoints.size(), 1);
    ASSERT_TRUE(std::holds_alternative<Ipv4Endpoint>(endpoints[0]));
    EXPECT_EQ(ToString(endpoints[0]), "[127.0.0.1]:1234");
}

TEST(DnsResolverTest, ResolveEndpointsIPv6) {
    ASSERT_OK_AND_ASSIGN(auto endpoints, ResolveEndpoints("[::1]:5678"));
    ASSERT_EQ(endpoints.size(), 1);
    ASSERT_TRUE(std::holds_alternative<Ipv6Endpoint>(endpoints[0]));
    EXPECT_EQ(ToString(endpoints[0]), "[::1]:5678");
}

TEST(DnsResolverTest, ResolveHostnameLocalhost) {
    ASSERT_OK_AND_ASSIGN(auto addresses, ResolveHostname("localhost"));
    EXPECT_GT(addresses.size(), 0);

    for (const auto& ip : addresses) {
        const std::string ip_str = ToString(ip);
        EXPECT_TRUE(ip_str == "127.0.0.1" || ip_str == "::1");
    }
}

TEST(DnsResolverTest, ResolveHostnameLocalhostIPV4) {
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    ASSERT_OK_AND_ASSIGN(auto addresses, ResolveHostname("localhost", &hints));
    EXPECT_GT(addresses.size(), 0);

    for (const auto& ip : addresses) {
        EXPECT_TRUE(std::holds_alternative<struct in_addr>(ip));
    }
}

TEST(DnsResolverTest, ResolveHostnameInvalid) {
    auto result = ResolveHostname("this-is-not-a-real-hostname-for-sure.com");
    EXPECT_THAT(result, StatusIs(absl::StatusCode::kNotFound));
}

TEST(DnsResolverTest, GetSystemDnsServers_SanityCheck) {
    // This test queries the actual OS configuration.
    auto result = GetSystemDnsServers();

    // Handle environments (like strict jail/chroot CI) that might lack /etc/resolv.conf
    if (absl::IsNotFound(result.status())) {
        SUCCEED() << "Skipping test: Host environment has no DNS configuration.";
        return;
    }

    ASSERT_OK(result.status());
    const auto& servers = *result;

    // If the OS reports success, we generally expect at least one server,
    // though a local-only environment might return an empty list with OK status
    // depending on the c-ares implementation details.
    if (servers.empty()) {
        LOG(INFO) << "getSystemDnsServers returned OK but found 0 servers.";
    }

    for (const auto& ip : servers) {
        LOG(INFO) << "Found System DNS: " << ToString(ip);
        EXPECT_FALSE(ToString(ip).empty());
    }
}

TEST(DnsResolverTest, GetSystemDnsServers_RepeatedCalls) {
    // This test ensures that the internal c-ares initialization and teardown (RAII)
    // works correctly and doesn't crash or leak when called multiple times.
    for (int i = 0; i < 5; ++i) {
        auto result = GetSystemDnsServers();
        if (result.ok()) {
            for (const auto& ip : *result) {
                LOG(INFO) << "Found System DNS: " << ToString(ip);
                EXPECT_FALSE(ToString(ip).empty());
            }
        }
    }
}

}  // namespace goldfish::network
