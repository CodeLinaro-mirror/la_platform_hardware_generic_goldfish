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

#include <gtest/gtest.h>

#include "absl/status/status_matchers.h"

#include "aemu/base/utils/status_matcher_macros.h"

namespace goldfish::network {

using absl_testing::IsOkAndHolds;
using absl_testing::StatusIs;

using testing::HasSubstr;

TEST(EndpointTest, ipv4) {
    const Ipv4Endpoint ep4 = ToIpEndpoint(ToIpv4Address(192, 168, 1, 42), 1234);
    const struct sockaddr_in sa4 = ToSockaddr(ep4);
    absl::StatusOr<Endpoint> sep = ToEndpoint(reinterpret_cast<const struct sockaddr&>(sa4));
    EXPECT_THAT(sep, IsOkAndHolds(ep4));
    const Endpoint ep = *std::move(sep);

    EXPECT_EQ(ToString(ep), "[192.168.1.42]:1234");

    const struct sockaddr_storage storage = ToSockaddr(ep);
    EXPECT_TRUE(::memcmp(&storage, &sa4, sizeof(sa4)) == 0);
}

TEST(EndpointTest, ipv6) {
    const Ipv6Endpoint ep6 = ToIpEndpoint(*ToIpv6Address("2001:db8:85a3::8a2e:370:7334"), 1234);
    const struct sockaddr_in6 sa6 = ToSockaddr(ep6);
    absl::StatusOr<Endpoint> sep = ToEndpoint(reinterpret_cast<const struct sockaddr&>(sa6));
    EXPECT_THAT(sep, IsOkAndHolds(ep6));
    const Endpoint ep = *std::move(sep);

    EXPECT_EQ(ToString(ep), "[2001:db8:85a3::8a2e:370:7334]:1234");

    const struct sockaddr_storage storage = ToSockaddr(ep);
    EXPECT_TRUE(::memcmp(&storage, &sa6, sizeof(sa6)) == 0);
}

TEST(EndpointTest, un) {
    const UnEndpoint unep = *ToUnEndpoint("abc");
    const struct sockaddr_un sun = ToSockaddr(unep);
    absl::StatusOr<Endpoint> sep = ToEndpoint(reinterpret_cast<const struct sockaddr&>(sun));
    EXPECT_THAT(sep, IsOkAndHolds(unep));
    const Endpoint ep = *std::move(sep);

    EXPECT_EQ(ToString(ep), "abc");

    const struct sockaddr_storage storage = ToSockaddr(ep);
    EXPECT_TRUE(::memcmp(&storage, &sun, sizeof(sun)) == 0);
}

TEST(EndpointTest, invalid) {
    struct sockaddr invalid;
    invalid.sa_family = AF_UNSPEC;

    EXPECT_THAT(ToEndpoint(invalid), StatusIs(absl::StatusCode::kInvalidArgument,
                                              HasSubstr("Unsupported address family")));
}

TEST(EndpointTest, GetPortFromEndpoint) {
    EXPECT_EQ(GetPortFromEndpoint(Ipv4Endpoint{.port = 1234}), 1234);
    EXPECT_EQ(GetPortFromEndpoint(Ipv6Endpoint{.port = 1234}), 1234);
    EXPECT_EQ(GetPortFromEndpoint(*UnEndpoint::Create("something")), -1);
}

}  // namespace goldfish::network