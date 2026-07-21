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
#include "android/base/testing/test_system.h"
#include "android/base/testing/test_temp_dir.h"
#include "emulator_controller.grpc.pb.h"
#include "emulator_controller_mock.grpc.pb.h"
#include "goldfish/discovery/emulator_advertisement.h"
#include "goldfish/file/file.h"
#include "modem_service_mock.grpc.pb.h"
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

    absl::StatusOr<std::unique_ptr<android::emulation::control::incubating::Modem::StubInterface>>
    ModemStub() override {
        if (mock_modem_stub) {
            return std::move(mock_modem_stub);
        }
        return ConsoleContext::ModemStub();
    }

    absl::StatusOr<std::unique_ptr<grpc::ClientContext>> NewContext(
            std::chrono::time_point<std::chrono::system_clock> deadline =
                    std::chrono::system_clock::now() + std::chrono::milliseconds(500)) override {
        auto ctx = std::make_unique<grpc::ClientContext>();
        ctx->set_deadline(deadline);
        return ctx;
    }

    absl::StatusOr<std::vector<std::filesystem::path>> DiscoverRunningEmulators() override {
        if (mock_discovered_emulators) {
            return *mock_discovered_emulators;
        }
        return ConsoleContext::DiscoverRunningEmulators();
    }

    absl::StatusOr<LegacyConsoleBridge::DiscoveredEmulator> DiscoverEmulatorWithProperties(
            const absl::flat_hash_map<std::string, std::string>& props) override {
        if (mock_discovery) {
            return *mock_discovery;
        }
        return ConsoleContext::DiscoverEmulatorWithProperties(props);
    }

    std::unique_ptr<android::emulation::control::EmulatorController::StubInterface> mock_stub;
    std::unique_ptr<android::emulation::control::incubating::Modem::StubInterface> mock_modem_stub;
    std::optional<absl::StatusOr<LegacyConsoleBridge::DiscoveredEmulator>> mock_discovery;
    std::optional<absl::StatusOr<std::vector<std::filesystem::path>>> mock_discovered_emulators;
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

TEST_F(LegacyConsoleBridgeTest, PingSucceedsAndReturnsAlive) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("ping", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "I am alive!");
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

TEST_F(LegacyConsoleBridgeTest, FingerTouchSendsGrpcRequest) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, sendFingerprint(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::Fingerprint& request,
                         google::protobuf::Empty* response) {
                EXPECT_TRUE(request.istouching());
                EXPECT_EQ(request.touchid(), 1);
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("finger touch 1", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(LegacyConsoleBridgeTest, FingerRemoveSendsGrpcRequest) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, sendFingerprint(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::Fingerprint& request,
                         google::protobuf::Empty* response) {
                EXPECT_FALSE(request.istouching());
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("finger remove", *ctx);

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

TEST_F(LegacyConsoleBridgeTest, IdSucceedsAndReturnsAvdId) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, getStatus(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const google::protobuf::Empty& request,
                         android::emulation::control::EmulatorStatus* response) {
                auto* config = response->mutable_platformconfig();
                (*config)["avd.id"] = "avd_id_123";
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd id", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "avd_id_123");
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

TEST_F(LegacyConsoleBridgeTest, PathSucceedsAndReturnsAvdPath) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, getStatus(_, _, _))
            .WillOnce([](grpc::ClientContext* context, const google::protobuf::Empty& request,
                         android::emulation::control::EmulatorStatus* response) {
                auto* config = response->mutable_platformconfig();
                (*config)["avd.content_path"] = "/path/to/avd";
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd path", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "/path/to/avd");
}

TEST_F(LegacyConsoleBridgeTest, PathReturnsErrorIfAvdPathMissingInPlatformConfig) {
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

    auto result = (*bridge_)("avd path", *ctx);

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kInternal);
}

TEST_F(LegacyConsoleBridgeTest, AvdGrpcSucceedsWhenEmulatorIsActive) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    LegacyConsoleBridge::DiscoveredEmulator mock_res;
    mock_res.discovery_file = "/path/to/dummy.ini";
    mock_res.properties["port.serial"] = "5554";
    mock_res.properties["grpc.port"] = "8554";
    ctx->mock_discovery = mock_res;

    auto result = (*bridge_)("avd grpc", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "8554");
}

TEST_F(LegacyConsoleBridgeTest, AvdGrpcFailsWhenNoActiveGrpcService) {
    auto ctx = CreateContext();
    ctx->authenticated = true;
    ctx->mock_discovery = absl::NotFoundError("No matching emulator found");

    auto result = (*bridge_)("avd grpc", *ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(LegacyConsoleBridgeTest, AvdGrpcFailsWhenMultipleEmulatorsMatch) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto file1 = tmpdir_.Path() / "pid_1.ini";
    auto file2 = tmpdir_.Path() / "pid_2.ini";

    std::ofstream ofs1(file1);
    ofs1 << "port.serial=5554\n";
    ofs1 << "grpc.port=8554\n";
    ofs1.close();

    std::ofstream ofs2(file2);
    ofs2 << "port.serial=5554\n";
    ofs2 << "grpc.port=8555\n";
    ofs2.close();

    ctx->mock_discovered_emulators = std::vector<std::filesystem::path>{file1, file2};

    auto result = (*bridge_)("avd grpc", *ctx);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), absl::StatusCode::kFailedPrecondition);
    EXPECT_TRUE(result.status().message().find("Multiple matching emulators found") !=
                std::string::npos);
}

TEST_F(LegacyConsoleBridgeTest, KillSucceedsAndCallsSetVmState) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, setVmState(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::VmRunState& request,
                         google::protobuf::Empty* response) {
                EXPECT_EQ(request.state(), android::emulation::control::VmRunState::SHUTDOWN);
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("kill", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

TEST_F(LegacyConsoleBridgeTest, ResumeSucceedsAndCallsSetVmState) {
    auto ctx = CreateContext();
    ctx->authenticated = true;

    auto mock_stub = std::make_unique<android::emulation::control::MockEmulatorControllerStub>();
    EXPECT_CALL(*mock_stub, setVmState(_, _, _))
            .WillOnce([](grpc::ClientContext* context,
                         const android::emulation::control::VmRunState& request,
                         google::protobuf::Empty* response) {
                EXPECT_EQ(request.state(), android::emulation::control::VmRunState::RUNNING);
                return grpc::Status::OK;
            });
    ctx->mock_stub = std::move(mock_stub);

    auto result = (*bridge_)("avd resume", *ctx);

    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, "");
}

}  // namespace
}  // namespace goldfish::telnet
