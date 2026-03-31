// Copyright 2026 The Android Open Source Project
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

#include "android/emulation/control/service_forwarder_impl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "service_forwarder.grpc.pb.h"

namespace android::emulation::forwarding {

using ::testing::SizeIs;

class ServiceForwarderImplTest : public ::testing::Test {
  protected:
    ServiceForwarderImpl forwarder_;
};

TEST_F(ServiceForwarderImplTest, RegisterAndRetrieveEndpoint) {
    ForwardingRule rule;
    rule.set_service_uri("my.service.uri");
    rule.mutable_endpoint()->set_target("localhost:1234");

    const grpc::Status status = forwarder_.registerForwarder(nullptr, &rule, nullptr);
    EXPECT_TRUE(status.ok());

    auto endpoint = forwarder_.GetEndpoint("my.service.uri");
    ASSERT_TRUE(endpoint.has_value());
    EXPECT_EQ(endpoint->target(), "localhost:1234");  // NOLINT(bugprone-unchecked-optional-access)
}

TEST_F(ServiceForwarderImplTest, GetEndpointNotFound) {
    auto endpoint = forwarder_.GetEndpoint("non.existent.uri");
    EXPECT_FALSE(endpoint.has_value());
}

TEST_F(ServiceForwarderImplTest, OverrideForwarder) {
    ForwardingRule rule1;
    rule1.set_service_uri("my.service.uri");
    rule1.mutable_endpoint()->set_target("localhost:1234");

    forwarder_.registerForwarder(nullptr, &rule1, nullptr);

    ForwardingRule rule2;
    rule2.set_service_uri("my.service.uri");
    rule2.mutable_endpoint()->set_target("localhost:5678");

    forwarder_.registerForwarder(nullptr, &rule2, nullptr);

    auto endpoint = forwarder_.GetEndpoint("my.service.uri");
    ASSERT_TRUE(endpoint.has_value());
    EXPECT_EQ(endpoint->target(), "localhost:5678");  // NOLINT(bugprone-unchecked-optional-access)
}

TEST_F(ServiceForwarderImplTest, ListForwardingRules) {
    ForwardingRule rule1;
    rule1.set_service_uri("service.1");
    rule1.mutable_endpoint()->set_target("target1");
    forwarder_.registerForwarder(nullptr, &rule1, nullptr);

    ForwardingRule rule2;
    rule2.set_service_uri("service.2");
    rule2.mutable_endpoint()->set_target("target2");
    forwarder_.registerForwarder(nullptr, &rule2, nullptr);

    ForwardingRuleList response;
    const grpc::Status status = forwarder_.listForwardingRules(nullptr, nullptr, &response);
    EXPECT_TRUE(status.ok());

    EXPECT_THAT(response.rules(), SizeIs(2));

    // Convert to map for easier verification
    std::map<std::string, std::string> rules;
    for (const auto& r : response.rules()) {
        rules[r.service_uri()] = r.endpoint().target();
    }

    EXPECT_EQ(rules["service.1"], "target1");
    EXPECT_EQ(rules["service.2"], "target2");
}

}  // namespace android::emulation::forwarding
