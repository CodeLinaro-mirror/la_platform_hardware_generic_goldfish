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
#include "goldfish/network/un_endpoint.h"

#include <gtest/gtest.h>

namespace goldfish::network {

TEST(UnEndpointTest, ToUnEndpoint_str) {
    const absl::StatusOr<UnEndpoint> ep = ToUnEndpoint("abc");
    ASSERT_TRUE(ep.ok());
    EXPECT_EQ(ToString(*ep), "abc");
}

TEST(UnEndpointTest, ToUnEndpoint_str_long) {
    EXPECT_TRUE(ToUnEndpoint(std::string(UnEndpoint::kMaxSize - 1, '?')).ok());
    EXPECT_TRUE(ToUnEndpoint(std::string(UnEndpoint::kMaxSize, '?')).ok());
    EXPECT_FALSE(ToUnEndpoint(std::string(UnEndpoint::kMaxSize + 1, '?')).ok());
}

TEST(UnEndpointTest, ToUnEndpoint_sockaddr) {
    struct sockaddr_un sun;
    ::memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    ::strcpy(sun.sun_path, "sockaddr_un");
    EXPECT_EQ(ToString(ToUnEndpoint(sun)), "sockaddr_un");
}

TEST(UnEndpointTest, ToUnEndpoint_sockaddr_linux_abstract) {
    static const char kAbstractAddress[] = {0, 'a', 'b', 'c'};

    struct sockaddr_un sun_src;
    ::memset(&sun_src, 0, sizeof(sun_src));
    sun_src.sun_family = AF_UNIX;
    ::memcpy(sun_src.sun_path, kAbstractAddress, sizeof(kAbstractAddress));

    const struct sockaddr_un sun_dst = ToSockaddr(ToUnEndpoint(sun_src));

    EXPECT_TRUE(::memcmp(&sun_src, &sun_dst, sizeof(sun_src)) == 0);
}

}  // namespace goldfish::network