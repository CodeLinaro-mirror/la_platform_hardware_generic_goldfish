// Copyright (C) 2026 The Android Open Source Project
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

#include "launch_netsimd.h"

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "android/emulation/control/emulator_grpc_client.h"

namespace android::goldfish::netsim {
namespace {

NetsimConnection_ptr CreateDummyConnection(const std::string& endpoint_target) {
    android::emulation::control::Endpoint endpoint;
    endpoint.set_target(endpoint_target);
    auto client = android::emulation::control::EmulatorGrpcClientBuilder()
                          .WithEndpoint(endpoint)
                          .BuildBlocking();
    return std::move(*client);
}

class NetsimConnectorTest : public testing::Test {
  protected:
    std::atomic<bool> shutting_down_{false};
};

TEST_F(NetsimConnectorTest, ExplicitEndpointSuccess) {
    std::string connected_endpoint;
    absl::Duration connected_deadline;
    bool launch_called = false;

    NetsimConnector connector(
            /*launch_netsimd_on_loop=*/
            [&]() {
                launch_called = true;
                return absl::OkStatus();
            },
            shutting_down_,
            /*force_existing_netsimd_endpoint=*/"localhost:1234",
            /*connect_fn=*/
            [&](const std::string& endpoint,
                absl::Duration deadline) -> absl::StatusOr<NetsimConnection_ptr> {
                connected_endpoint = endpoint;
                connected_deadline = deadline;
                return CreateDummyConnection(endpoint);
            },
            /*port_reader_fn=*/[]() { return 0; });

    auto result = connector.Run();
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(launch_called);
    EXPECT_EQ(connected_endpoint, "localhost:1234");
    EXPECT_EQ(connected_deadline, absl::Seconds(5));
}

TEST_F(NetsimConnectorTest, ExplicitEndpointFailure) {
    bool launch_called = false;

    NetsimConnector connector(
            /*launch_netsimd_on_loop=*/
            [&]() {
                launch_called = true;
                return absl::OkStatus();
            },
            shutting_down_,
            /*force_existing_netsimd_endpoint=*/"localhost:1234",
            /*connect_fn=*/
            [](const std::string&, absl::Duration) -> absl::StatusOr<NetsimConnection_ptr> {
                return absl::UnavailableError("connection failed");
            },
            /*port_reader_fn=*/[]() { return 0; });

    auto result = connector.Run();
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(launch_called);
    EXPECT_EQ(result.status().code(), absl::StatusCode::kUnavailable);
}

TEST_F(NetsimConnectorTest, ExistingPortConnectSuccess) {
    std::string connected_endpoint;
    absl::Duration connected_deadline;
    bool launch_called = false;

    NetsimConnector connector(
            /*launch_netsimd_on_loop=*/
            [&]() {
                launch_called = true;
                return absl::OkStatus();
            },
            shutting_down_,
            /*force_existing_netsimd_endpoint=*/std::nullopt,
            /*connect_fn=*/
            [&](const std::string& endpoint,
                absl::Duration deadline) -> absl::StatusOr<NetsimConnection_ptr> {
                connected_endpoint = endpoint;
                connected_deadline = deadline;
                return CreateDummyConnection(endpoint);
            },
            /*port_reader_fn=*/[]() { return 8554; });

    auto result = connector.Run();
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(launch_called);
    EXPECT_EQ(connected_endpoint, "localhost:8554");
    EXPECT_EQ(connected_deadline, absl::Seconds(5));
}

TEST_F(NetsimConnectorTest, ExistingPortDeadRetryAndLaunchNewInstance) {
    int read_count = 0;
    auto port_reader = [&]() {
        ++read_count;
        if (read_count == 1) {
            return 8554;  // Initial existing port (which will fail to connect)
        }
        return 8556;  // New port after launching fresh instance
    };

    int launch_count = 0;
    std::vector<std::string> connection_attempts;

    NetsimConnector connector(
            /*launch_netsimd_on_loop=*/
            [&]() {
                ++launch_count;
                return absl::OkStatus();
            },
            shutting_down_,
            /*force_existing_netsimd_endpoint=*/std::nullopt,
            /*connect_fn=*/
            [&](const std::string& endpoint,
                absl::Duration) -> absl::StatusOr<NetsimConnection_ptr> {
                connection_attempts.push_back(endpoint);
                if (endpoint == "localhost:8554") {
                    return absl::UnavailableError("Existing netsimd died");
                }
                return CreateDummyConnection(endpoint);
            },
            port_reader);

    auto result = connector.Run();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(launch_count, 1);
    ASSERT_EQ(connection_attempts.size(), 2);
    EXPECT_EQ(connection_attempts[0], "localhost:8554");
    EXPECT_EQ(connection_attempts[1], "localhost:8556");
}

TEST_F(NetsimConnectorTest, FreshLaunchSuccessWhenNoExistingPort) {
    int read_count = 0;
    auto port_reader = [&]() {
        ++read_count;
        if (read_count <= 2) {
            return 0;  // Port not yet written
        }
        return 8554;  // Port written by new instance
    };

    int launch_count = 0;
    std::string connected_endpoint;
    NetsimConnector connector(
            /*launch_netsimd_on_loop=*/
            [&]() {
                ++launch_count;
                return absl::OkStatus();
            },
            shutting_down_,
            /*force_existing_netsimd_endpoint=*/std::nullopt,
            /*connect_fn=*/
            [&](const std::string& endpoint,
                absl::Duration deadline) -> absl::StatusOr<NetsimConnection_ptr> {
                connected_endpoint = endpoint;
                EXPECT_EQ(deadline, absl::Seconds(5));
                return CreateDummyConnection(endpoint);
            },
            port_reader);

    auto result = connector.Run();
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(launch_count, 1);
    EXPECT_EQ(connected_endpoint, "localhost:8554");
}

TEST_F(NetsimConnectorTest, ProcessLaunchFailureReturnsError) {
    int launch_count = 0;
    NetsimConnector connector(
            /*launch_netsimd_on_loop=*/
            [&]() {
                ++launch_count;
                return absl::InternalError("Failed to spawn netsimd");
            },
            shutting_down_,
            /*force_existing_netsimd_endpoint=*/std::nullopt,
            /*connect_fn=*/
            [](const std::string&, absl::Duration) -> absl::StatusOr<NetsimConnection_ptr> {
                return absl::InternalError("Should not be called");
            },
            /*port_reader_fn=*/[]() { return 0; });

    auto result = connector.Run();
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(launch_count, 1);
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInternal);
}

TEST_F(NetsimConnectorTest, NewlyLaunchedConnectionFailureReturnsError) {
    int launch_count = 0;
    int read_count = 0;
    auto port_reader = [&]() {
        ++read_count;
        if (read_count == 1) {
            return 8554;  // Existing port
        }
        return 8556;  // New port after launch
    };

    NetsimConnector connector(
            /*launch_netsimd_on_loop=*/
            [&]() {
                ++launch_count;
                return absl::OkStatus();
            },
            shutting_down_,
            /*force_existing_netsimd_endpoint=*/std::nullopt,
            /*connect_fn=*/
            [](const std::string& endpoint,
               absl::Duration) -> absl::StatusOr<NetsimConnection_ptr> {
                return absl::UnavailableError("Failed to connect to instance");
            },
            port_reader);

    // When existing port connection fails, it launches a new instance, but new connection fails too
    auto result = connector.Run();
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(launch_count, 1);
    EXPECT_EQ(result.status().code(), absl::StatusCode::kUnavailable);
}

TEST_F(NetsimConnectorTest, ShuttingDownAbortsCleanly) {
    shutting_down_ = true;

    NetsimConnector connector(
            /*launch_netsimd_on_loop=*/[]() { return absl::InternalError("Should not be called"); },
            shutting_down_,
            /*force_existing_netsimd_endpoint=*/std::nullopt,
            /*connect_fn=*/
            [](const std::string&, absl::Duration) -> absl::StatusOr<NetsimConnection_ptr> {
                return absl::InternalError("Should not be called");
            },
            /*port_reader_fn=*/[]() { return 0; });

    auto result = connector.Run();
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kCancelled);
}

}  // namespace
}  // namespace android::goldfish::netsim
