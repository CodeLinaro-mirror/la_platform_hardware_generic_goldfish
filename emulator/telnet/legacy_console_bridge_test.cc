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

#include "legacy_console_bridge.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#include "android/base/system.h"
#include "android/base/testing/TestSystem.h"
#include "android/base/testing/TestTempDir.h"
#include "emulator_controller.grpc.pb.h"
#include "emulator_controller_mock.grpc.pb.h"
#include "goldfish/file/file.h"
#include "telnet_auth.h"

namespace goldfish::telnet {
namespace {

using testing::_;

struct MockConsoleContext : public LegacyConsoleBridge::ConsoleContext {
    using ConsoleContext::ConsoleContext;

    absl::StatusOr<std::unique_ptr<android::emulation::control::EmulatorController::StubInterface>>
    EmulatorControllerStub() override {
        if (mock_stub) {
            return std::move(mock_stub);
        }
        return ConsoleContext::EmulatorControllerStub();
    }

    absl::StatusOr<std::unique_ptr<grpc::ClientContext>> NewContext(
            std::chrono::time_point<std::chrono::system_clock> deadline =
                    std::chrono::system_clock::now() + std::chrono::milliseconds(500)) override {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(deadline);
        return ctx;
    }

    std::unique_ptr<android::emulation::control::EmulatorController::StubInterface> mock_stub;
};

class LegacyConsoleBridgeTest : public ::testing::Test {
  protected:
    void SetUp() override {
        test_home_ = tmpdir_.Path() / "test_home_auth";
        auto discovery_dir = test_home_ / "Library/Caches/TemporaryItems/avd/running";
        auto status = android::base::file::mkdir_recursive(discovery_dir, 0700);
        ASSERT_TRUE(status.ok()) << "Failed to create test home directory: " << status.message();
        test_system_.SetHomeDirectory(test_home_);
        token_path_ = (test_home_ / ".emulator_console_auth_token").string();

        WriteToken("valid_token_123");

        bridge_ = std::make_unique<LegacyConsoleBridge>(5554, token_path_);
    }

    std::unique_ptr<MockConsoleContext> CreateContext(int port = 5554) {
        return std::make_unique<MockConsoleContext>(port);
    }

    void WriteToken(const std::string& token) {
        std::ofstream ofs(token_path_);
        ofs << token;
    }

    android::base::TestTempDir tmpdir_{"LegacyConsoleBridgeTest"};
    android::base::TestSystem test_system_{"/foo/bar"};
    std::filesystem::path test_home_;
    std::string token_path_;
    std::unique_ptr<LegacyConsoleBridge> bridge_;
};

TEST_F(LegacyConsoleBridgeTest, AuthSucceedsWithValidToken) {
    LegacyConsoleBridge::ConsoleContext ctx(5554);
    EXPECT_FALSE(ctx.authenticated);

    auto result = (*bridge_)("auth valid_token_123", ctx);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(*result, "Android Console: type 'help' for a list of commands");
    EXPECT_TRUE(ctx.authenticated);
}

TEST_F(LegacyConsoleBridgeTest, AuthFailsWithInvalidToken) {
    LegacyConsoleBridge::ConsoleContext ctx(5554);
    EXPECT_FALSE(ctx.authenticated);

    auto result = (*bridge_)("auth wrong_token", ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(result.status().message().find("authentication token does not match") !=
                std::string::npos);
    EXPECT_FALSE(ctx.authenticated);
}

TEST_F(LegacyConsoleBridgeTest, HelpReturnsDifferentListAfterAuth) {
    LegacyConsoleBridge::ConsoleContext ctx(5554);

    auto result_before = (*bridge_)("help", ctx);
    ASSERT_TRUE(result_before.ok());

    auto auth_result = (*bridge_)("auth valid_token_123", ctx);
    ASSERT_TRUE(auth_result.ok());

    auto result_after = (*bridge_)("help", ctx);
    ASSERT_TRUE(result_after.ok());

    EXPECT_NE(*result_before, *result_after);
}

TEST_F(LegacyConsoleBridgeTest, PingFailsWhenNoEmulatorFound) {
    LegacyConsoleBridge::ConsoleContext ctx(5554);
    ctx.authenticated = true;  // Safe to call commands

    auto result = (*bridge_)("ping", ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(LegacyConsoleBridgeTest, GeoFixFailsWhenNoEmulatorFound) {
    LegacyConsoleBridge::ConsoleContext ctx(5554);
    ctx.authenticated = true;  // Safe to call commands

    auto result = (*bridge_)("geo fix 12.3 45.6", ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(LegacyConsoleBridgeTest, GeoFixFailsWithInvalidCoordinates) {
    LegacyConsoleBridge::ConsoleContext ctx(5554);
    ctx.authenticated = true;  // Safe to call commands

    auto result = (*bridge_)("geo fix 200.0 45.6", ctx);  // Invalid longitude

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(result.status().message().find("Longitude must be between -180 and 180") !=
                std::string::npos);

    result = (*bridge_)("geo fix 12.3 100.0", ctx);  // Invalid latitude

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
    EXPECT_TRUE(result.status().message().find("Latitude must be between -90 and 90") !=
                std::string::npos);
}

TEST_F(LegacyConsoleBridgeTest, GeoFixSucceedsWithValidCoordinates) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, setGps(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::GpsState& request,
                         google::protobuf::Empty* response) {
                EXPECT_DOUBLE_EQ(request.longitude(), 12.3);
                EXPECT_DOUBLE_EQ(request.latitude(), 45.6);
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("geo fix 12.3 45.6", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(LegacyConsoleBridgeTest, HeartbeatFailsWhenNotAuthenticated) {
    LegacyConsoleBridge::ConsoleContext ctx(5554);
    EXPECT_FALSE(ctx.authenticated);

    auto result = (*bridge_)("avd heartbeat", ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
    EXPECT_TRUE(result.status().message().find("unknown command") != std::string::npos);
}

TEST_F(LegacyConsoleBridgeTest, HeartbeatFailsWhenNoEmulatorFound) {
    LegacyConsoleBridge::ConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result = (*bridge_)("avd heartbeat", ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(LegacyConsoleBridgeTest, HeartbeatSucceedsAndReturnsStatus) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, getStatus(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const google::protobuf::Empty& request,
                         android::emulation::control::EmulatorStatus* response) {
                response->set_heartbeat(42);
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd heartbeat", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "heartbeat: 42");
}

TEST_F(LegacyConsoleBridgeTest, NameFailsWhenNoEmulatorFound) {
    LegacyConsoleBridge::ConsoleContext ctx(5554);
    ctx.authenticated = true;

    auto result = (*bridge_)("avd name", ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(LegacyConsoleBridgeTest, NameSucceedsAndReturnsAvdName) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, getStatus(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const google::protobuf::Empty& request,
                         android::emulation::control::EmulatorStatus* response) {
                auto* config = response->mutable_platformconfig();
                (*config)["avd.name"] = "Pixel_9_Pro";
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd name", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "Pixel_9_Pro");
}

TEST_F(LegacyConsoleBridgeTest, NameReturnsUnknownIfAvdNameMissingInPlatformConfig) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, getStatus(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const google::protobuf::Empty& request,
                         android::emulation::control::EmulatorStatus* response) {
                auto* config = response->mutable_platformconfig();
                (*config)["other.property"] = "some_value";
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd name", *ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInternal);
}

}  // namespace
}  // namespace goldfish::telnet
