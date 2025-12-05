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

#include "aemu/base/utils/status_matcher_macros.h"

#include "android/base/testing/NeedsWinsock.h"

#include "goldfish/network/endpoint.h"
#include "goldfish/network/ip_address.h"

namespace goldfish::network {

using absl_testing::StatusIs;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;

TEST(DnsResolverTest, ResolveEndpointsLocalhost) {
    ASSERT_OK_AND_ASSIGN(auto endpoints, resolveEndpoints("localhost:8080"));
    EXPECT_GT(endpoints.size(), 0);
    for (const auto& endpoint : endpoints) {
        EXPECT_EQ(endpoint.port(), 8080);
        EXPECT_TRUE(endpoint.address().toString() == "127.0.0.1" ||
                    endpoint.address().toString() == "::1");
    }
}

TEST(DnsResolverTest, ResolveEndpointsIPv4) {
    ASSERT_OK_AND_ASSIGN(auto endpoints, resolveEndpoints("127.0.0.1:1234"));
    ASSERT_EQ(endpoints.size(), 1);
    EXPECT_EQ(endpoints[0].address().toString(), "127.0.0.1");
    EXPECT_EQ(endpoints[0].port(), 1234);
    EXPECT_EQ(endpoints[0].address().family(), IpAddress::Family::kIpv4);
}

TEST(DnsResolverTest, ResolveEndpointsIPv6) {
    ASSERT_OK_AND_ASSIGN(auto endpoints, resolveEndpoints("[::1]:5678"));
    ASSERT_EQ(endpoints.size(), 1);
    EXPECT_EQ(endpoints[0].address().toString(), "::1");
    EXPECT_EQ(endpoints[0].port(), 5678);
    EXPECT_EQ(endpoints[0].address().family(), IpAddress::Family::kIpv6);
}

TEST(DnsResolverTest, ResolveHostnameLocalhost) {
    ASSERT_OK_AND_ASSIGN(auto addresses, resolveHostname("localhost"));
    EXPECT_GT(addresses.size(), 0);

    for (const auto& ip : addresses) {
        EXPECT_TRUE(ip.toString() == "127.0.0.1" || ip.toString() == "::1");
    }
}

TEST(DnsResolverTest, ResolveHostnameLocalhostIPV4) {
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    ASSERT_OK_AND_ASSIGN(auto addresses, resolveHostname("localhost", &hints));
    EXPECT_GT(addresses.size(), 0);

    for (const auto& ip : addresses) {
        EXPECT_EQ(ip.family(), IpAddress::Family::kIpv4);
    }
}

TEST(DnsResolverTest, ResolveHostnameInvalid) {
    auto result = resolveHostname("this-is-not-a-real-hostname-for-sure.com");
    EXPECT_THAT(result, StatusIs(absl::StatusCode::kNotFound));
}

TEST(DnsResolverTest, GetSystemDnsServers_SanityCheck) {
    // This test queries the actual OS configuration.
    auto result = getSystemDnsServers();

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
        LOG(INFO) << "Found System DNS: " << ip;
        EXPECT_FALSE(ip.toString().empty());
    }
}

TEST(DnsResolverTest, GetSystemDnsServers_RepeatedCalls) {
    // This test ensures that the internal c-ares initialization and teardown (RAII)
    // works correctly and doesn't crash or leak when called multiple times.
    for (int i = 0; i < 5; ++i) {
        auto result = getSystemDnsServers();
        if (result.ok()) {
            for (const auto& ip : *result) {
                LOG(INFO) << "Found System DNS: " << ip;
                EXPECT_FALSE(ip.toString().empty());
            }
        }
    }
}

}  // namespace goldfish::network
